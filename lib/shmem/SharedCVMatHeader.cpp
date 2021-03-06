//******************************************************************************
//* File:   SharedCVMatHeader.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//*
//* Copyright (c) Jon Newman (jpnewman snail mit dot edu) 
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
//****************************************************************************

#include <cstring>
#include <opencv2/core/mat.hpp>

#include "SharedCVMatHeader.h"

namespace oat {
    
    SharedCVMatHeader::SharedCVMatHeader() :
      mutex(1)
    , write_barrier(0)
    , read_barrier(0)
    , new_data_barrier(0)
    , sample_number(0) { }

    void SharedCVMatHeader::writeSample(const uint32_t sample, const cv::Mat& value) {
        
        std::memcpy(data_ptr, value.data, data_size_in_bytes);
        sample_number = sample;
    }
    
    void SharedCVMatHeader::buildHeader(boost::interprocess::managed_shared_memory& shared_mem, const cv::Mat& model) {

        data_size_in_bytes = model.total() * model.elemSize();
        data_ptr = shared_mem.allocate(data_size_in_bytes);
        mat_size = model.size();
        type = model.type();
        handle = shared_mem.get_handle_from_address(data_ptr);
    }

    void SharedCVMatHeader::attachMatToHeader(boost::interprocess::managed_shared_memory& shared_mem, cv::Mat& mat) {
        
        mat.create(mat_size, type);
        mat.data = static_cast<uchar*> (shared_mem.get_address_from_handle(handle));
    }

} // namespace oat
