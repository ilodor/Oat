//******************************************************************************
//* Copyright (c) Jon Newman (jpnewman at mit snail edu) 
//* All right reserved.
//* This file is part of the Oat project.
//* This is free software: you can redistribute it and/or modify
//* it under the terms of the GNU General Public License as published by
//* the Free Software Foundation, either version 3 of the License, or
//* (at your option) any later version.
//* This software is distributed in the hope that it will be useful,
//* but WITHOUT ANY WARRANTY; without even the implied warranty of
//* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//* GNU General Public License for more details.
//* You should have received a copy of the GNU General Public License
//* along with this source code.  If not, see <http://www.gnu.org/licenses/>.
//******************************************************************************

#ifndef SMSERVER_H
#define	SMSERVER_H

#include <string>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/thread/thread_time.hpp>

#include "../../lib/utility/IOFormat.h"
#include "SyncSharedMemoryObject.h"
#include "SharedMemoryManager.h"

namespace oat {

    namespace bip = boost::interprocess;

    template<class T, template <typename> class SharedMemType = oat::SyncSharedMemoryObject>
    class SMServer {
    public:
        SMServer(std::string sink_name);
        SMServer(const SMServer& orig);
        virtual ~SMServer();
        
        void pushObject(T value, uint32_t sample_number);

        
    private:

        // Name of this server
        std::string name;

        // Shared memory and managed object names
        SharedMemType<T>* shared_object; // Defaults to oat::SyncSharedMemoryObject<T>
        oat::SharedMemoryManager* shared_mem_manager;
        std::string shmem_name, shobj_name, shmgr_name;
        bip::managed_shared_memory shared_memory;
        bool shared_object_created;

        void createSharedObject(void);
        void notifySelf(void);

    };

    template<class T, template <typename> class SharedMemType>
    SMServer<T, SharedMemType>::SMServer(std::string sink_name) :
      name(sink_name)
    , shmem_name(sink_name + "_sh_mem")
    , shobj_name(sink_name + "_sh_obj")
    , shmgr_name(sink_name + "_sh_mgr")
    , shared_object_created(false) {
        
        createSharedObject();
    }

    template<class T, template <typename> class SharedMemType>
    SMServer<T, SharedMemType>::SMServer(const SMServer<T, SharedMemType>& orig) {
    }

    template<class T, template <typename> class SharedMemType>
    SMServer<T, SharedMemType>::~SMServer() {

        notifySelf();
        
        // Detach this server from shared mat header
        shared_mem_manager->set_server_state(oat::ServerRunState::END);

        // TODO: If the client ref count is 0, memory can be deallocated
        if (shared_mem_manager->get_client_ref_count() == 0) {
            
            // Remove_shared_memory on object destruction
            bip::shared_memory_object::remove(shmem_name.c_str());
#ifndef NDEBUG
            std::cout << oat::dbgMessage("Shared memory \'" + shmem_name + "\' was deallocated.\n");
#endif
        }       
    }

    template<class T, template <typename> class SharedMemType>
    void SMServer<T, SharedMemType>::createSharedObject() {

        // Allocate shared memory
        // Throws bip::interprocess_exception on failure
        shared_memory = bip::managed_shared_memory(
                bip::open_or_create,
                shmem_name.c_str(),
                sizeof (SharedMemType<T>) + sizeof (oat::SharedMemoryManager) + 1024);

        // Make the shared object
        shared_object = shared_memory.find_or_construct<SharedMemType < T >> (shobj_name.c_str())();
        shared_mem_manager = shared_memory.find_or_construct<oat::SharedMemoryManager>(shmgr_name.c_str())();

        // Make sure there is not another server using this shmem
        if (shared_mem_manager->get_server_state() != oat::ServerRunState::UNDEFINED) {
            
            // There is already a server using this shmem
            throw (std::runtime_error(
                   "Requested SINK name, '" + name + "', is not available."));
            
        } else {

            shared_object_created = true;
            shared_mem_manager->set_server_state(oat::ServerRunState::ATTACHED);
        }
    }

    /**
     * Push object into shared memory FIFO buffer.
     * 
     * @param value Object to store in shared memory. This object is copied on the FIFO.
     * @param sample_number The sample number associated with the object copied onto the FIFO.
     */
    template<class T, template <typename> class SharedMemType>
    void SMServer<T, SharedMemType>::pushObject(T value, uint32_t sample_number) {

#ifndef NDEBUG

        std::cout << oat::dbgMessage("sample: " + std::to_string(sample_number)) << "\r";
        std::cout.flush();

#endif
        boost::system_time timeout =
                boost::get_system_time() + boost::posix_time::milliseconds(10);

        try {
            /* START CRITICAL SECTION */
            shared_object->mutex.wait();

            // Perform writes in shared memory 
            shared_object->writeSample(sample_number, value);

            shared_object->mutex.post();
            /* END CRITICAL SECTION */

            // Tell each client they can proceed
            for (int i = 0; i < shared_mem_manager->get_client_ref_count(); ++i) {
                shared_object->read_barrier.post();
            }

            // Only wait if there is a client
            // Timed wait with period check to prevent deadlocks
            while (shared_mem_manager->get_client_ref_count() > 0 &&
                    !shared_object->write_barrier.timed_wait(timeout)) {
            }

            // Tell each client they can proceed now that the write_barrier
            // has been passed
            for (int i = 0; i < shared_mem_manager->get_client_ref_count(); ++i) {
                shared_object->new_data_barrier.post();
            }
            
        } catch (bip::interprocess_exception ex) {

            // Something went wrong during shmem access so result is invalid
            // Usually due to SIGINT being called during read_barrier timed
            // wait
            return;
        }
    }

    template<class T, template <typename> class SharedMemType>
    void SMServer<T, SharedMemType>::notifySelf() {

        if (shared_object_created) {
            shared_object->write_barrier.post();
        }
    }
}

#endif	/* SMSERVER_H */
