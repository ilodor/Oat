//******************************************************************************
//* File:   oat framefilt main.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//
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

#include "OatConfig.h" // Generated by CMake

#include <iostream>
#include <csignal>
#include <unordered_map>
#include <boost/program_options.hpp>
#include <opencv2/core.hpp>

#include "../../lib/utility/IOFormat.h"
#include "../../lib/cpptoml/cpptoml.h"

#include "FrameFilter.h"
#include "BackgroundSubtractor.h"
#include "BackgroundSubtractorMOG.h"
#include "FrameMasker.h"

namespace po = boost::program_options;

volatile sig_atomic_t quit = 0;
volatile sig_atomic_t source_eof = 0;

void printUsage(po::options_description options){
    std::cout << "Usage: framefilt [INFO]\n"
              << "   or: framefilt TYPE SOURCE SINK [CONFIGURATION]\n"
              << "Filter frames from SOURCE and published filtered frames "
              << "to SINK.\n\n"
              << "TYPE\n"
              << "  bsub: Background subtraction\n"
              << "  mask: Binary mask\n"
              << "   mog: Mixture of Gaussians background segmentation.\n\n"
              << "SOURCE:\n"
              << "  User-supplied name of the memory segment to receive frames "
              << "from (e.g. raw).\n\n"
              << "SINK:\n"
              << "  User-supplied name of the memory segment to publish frames "
              << "to (e.g. filt).\n\n"
              << options << "\n";
}

// Signal handler to ensure shared resources are cleaned on exit due to ctrl-c
void sigHandler(int s) {
    quit = 1;
}

// Processing loop
void run(const std::shared_ptr<FrameFilter>& frameFilter) {

    while (!quit && !source_eof) {
        source_eof = frameFilter->processSample();
    }
}

int main(int argc, char *argv[]) {

    std::signal(SIGINT, sigHandler);

    std::string type;
    std::string source;
    std::string sink;
    std::string config_file;
    std::string config_key;
    bool config_used = false;
    bool invert_mask = false;
    po::options_description visible_options("OPTIONS");

    std::unordered_map<std::string, char> type_hash;
    type_hash["bsub"] = 'a';
    type_hash["mask"] = 'b';
    type_hash["mog"] = 'c';
    
    try {

        po::options_description options("INFO");
        options.add_options()
                ("help", "Produce help message.")
                ("version,v", "Print version information.")
                ;
        
        po::options_description config("CONFIGURATION");
        config.add_options()
                ("config-file,c", po::value<std::string>(&config_file), "Configuration file.")
                ("config-key,k", po::value<std::string>(&config_key), "Configuration key.")
                ("invert-mask,m", "If using TYPE=mask, invert the mask before applying")
                ;

        po::options_description hidden("HIDDEN OPTIONS");
        hidden.add_options()
                ("type", po::value<std::string>(&type), 
                "Type of frame filter to use.\n\n"
                "Values:\n"
                "  bsub: Background subtractor.\n"
                "  mask: Binary mask.")
                ("source", po::value<std::string>(&source),
                "The name of the SOURCE that supplies images on which to perform background subtraction."
                "The server must be of type SMServer<SharedCVMatHeader>\n")
                ("sink", po::value<std::string>(&sink),
                "The name of the SINK to which background subtracted images will be served.")
                ;

        po::positional_options_description positional_options;
         positional_options.add("type", 1);
        positional_options.add("source", 1);
        positional_options.add("sink", 1);
        
        visible_options.add(options).add(config);

        po::options_description all_options("All options");
        all_options.add(options).add(config).add(hidden);

        po::variables_map variable_map;
        po::store(po::command_line_parser(argc, argv)
                .options(all_options)
                .positional(positional_options)
                .run(),
                variable_map);
        po::notify(variable_map);

        // Use the parsed options
        if (variable_map.count("help")) {
            printUsage(visible_options);
            return 0;
        }

        if (variable_map.count("version")) {
            std::cout << "Oat Frame Filter version "
                      << Oat_VERSION_MAJOR
                      << "." 
                      << Oat_VERSION_MINOR 
                      << "\n";
            std::cout << "Written by Jonathan P. Newman in the MWL@MIT.\n";
            std::cout << "Licensed under the GPL3.0.\n";
            return 0;
        }
        
        if (!variable_map.count("type")) {
            printUsage(visible_options);
            std::cout << oat::Error("A TYPE must be specified.\n");
            return -1;
        }

        if (!variable_map.count("source")) {
            printUsage(visible_options);
            std::cout << oat::Error("A SOURCE must be specified.\n");
            return -1;
        }

        if (!variable_map.count("sink")) {
            printUsage(visible_options);
            std::cerr << oat::Error("A SINK name must be specified.\n");
            return -1;
        }
              
        if (variable_map.count("invert-mask")) {

            if (type_hash[type] != 'b') {
                std::cerr << oat::Warn("Invert-mask specified, but this is the"
                          " wrong filter TYPE for that option.\n")
                          << oat::Warn("Invert-mask option was ignored.\n");
            } else {
                invert_mask = true;
            }
        }
            
        if ((variable_map.count("config-file") && !variable_map.count("config-key")) ||
                (!variable_map.count("config-file") && variable_map.count("config-key"))) {
            printUsage(visible_options);
            std::cerr << oat::Error("A configuration file must be supplied with a corresponding config-key.\n");
            return -1;
        } else if (variable_map.count("config-file")) {
            config_used = true;
        }

    } catch (std::exception& e) {
        std::cerr << oat::Error(e.what()) << "\n";
        return -1;
    } catch (...) {
        std::cerr << oat::Error("Exception of unknown type.\n");
        return -1;
    }

    // Create component
    std::shared_ptr<FrameFilter> filter;
    
    // Refine component type
    switch (type_hash[type]) {
        case 'a':
        {
            filter = std::make_shared<BackgroundSubtractor>(source, sink);
            break;
        }
        case 'b':
        {
            filter = std::make_shared<FrameMasker>(source, sink, invert_mask);
            if (!config_used)
                 std::cerr << oat::whoWarn(filter->get_name(), 
                         "No mask configuration was provided." 
                         " This filter does nothing but waste CPU cycles.\n");
            break;
        }
        case 'c':
        {
            filter = std::make_shared<BackgroundSubtractorMOG>(source, sink);
            break;
        }
        default:
        {
            printUsage(visible_options);
            std::cerr << oat::Error("Invalid TYPE specified.\n");
            return -1;
        }
    }
    
    // The business
    try { 
        
        if (config_used)
            filter->configure(config_file, config_key);

        // Tell user
        std::cout << oat::whoMessage(filter->get_name(),
                "Listening to source " + oat::sourceText(source) + ".\n")
                << oat::whoMessage(filter->get_name(),
                "Steaming to sink " + oat::sinkText(sink) + ".\n")
                << oat::whoMessage(filter->get_name(),
                "Press CTRL+C to exit.\n");

        // Infinite loop until ctrl-c or end of stream signal
        run(filter);

        // Tell user
        std::cout << oat::whoMessage(filter->get_name(), "Exiting.\n");
        
        // Exit success
        return 0;

    } catch (const cpptoml::parse_exception& ex) {
        std::cerr << oat::whoError(filter->get_name(), 
                     "Failed to parse configuration file " 
                     + config_file + "\n")
                  << oat::whoError(filter->get_name(), ex.what())
                  << "\n";
    } catch (const std::runtime_error ex) {
        std::cerr << oat::whoError(filter->get_name(),ex.what())
                  << "\n";
    } catch (const cv::Exception ex) {
        std::cerr << oat::whoError(filter->get_name(), ex.what())
                  << "\n";
    } catch (...) {
        std::cerr << oat::whoError(filter->get_name(), "Unknown exception.\n");
    }
    
    // Exit failure
    return -1;

}
