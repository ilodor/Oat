//******************************************************************************
//* File:   oat positest main.cpp
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
#include <unordered_map>
#include <time.h>
#include <csignal>
#include <boost/program_options.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>

#include "../../lib/utility/IOFormat.h"
#include "../../lib/cpptoml/cpptoml.h"

#include "TestPosition.h"
#include "RandomAccel2D.h"

namespace po = boost::program_options;

volatile sig_atomic_t quit = 0;
volatile sig_atomic_t source_eof = 0;

void printUsage(po::options_description options) {
    std::cout << "Usage: positest [INFO]\n"
              << "   or: positest TYPE SINK [CONFIGURATION]\n"
              << "Publish test positions to SINK.\n\n"
              << "TYPE\n"
              << "  rand2D: Randomly accelerating 2D Position\n\n"
              << "SINK:\n"
              << "  User supplied position sink name (e.g. pos).\n\n"
              << options << "\n";
}

// Signal handler to ensure shared resources are cleaned on exit due to ctrl-c
void sigHandler(int s) {
    quit = 1;
}

// Processing loop
void run(std::shared_ptr< TestPosition<oat::Position2D> > test_position) {

    while (!quit && !source_eof) {
        source_eof = test_position->process();
    }
}

// IO thread
int main(int argc, char *argv[]) {

    std::signal(SIGINT, sigHandler);

    std::string sink;
    std::string type;
    double samples_per_second = 30;
    std::string config_file;
    std::string config_key;
    bool config_used = false;
    po::options_description visible_options("OPTIONS");
    
    std::unordered_map<std::string, char> type_hash;
    type_hash["rand2D"] = 'a';

    try {

        po::options_description options("INFO");
        options.add_options()
                ("help", "Produce help message.")
                ("version,v", "Print version information.")
                ;

        po::options_description config("CONFIGURATION");
        config.add_options()
                ("sps,r", po::value<double>(&samples_per_second),
                "Samples per second. Overriden by information in configuration file if provided.")
                ("config-file,c", po::value<std::string>(&config_file), "Configuration file.")
                ("config-key,k", po::value<std::string>(&config_key), "Configuration key.")
                ;

        po::options_description hidden("HIDDEN OPTIONS");
        hidden.add_options()
                ("type", po::value<std::string>(&type), 
                "Type of test position to serve.")
                ("sink", po::value<std::string>(&sink),
                "The name of the SINK to which position background subtracted images will be served.")
                ;

        po::positional_options_description positional_options;
        positional_options.add("type", 1);
        positional_options.add("sink", 1);

        po::options_description visible_options("VISIBLE OPTIONS");
        visible_options.add(options).add(config);

        po::options_description all_options("ALL OPTIONS");
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
            std::cout << "Oat Test-Position Server version "
                      << Oat_VERSION_MAJOR
                      << "." 
                      << Oat_VERSION_MINOR 
                      << "\n"
                      << "Written by Jonathan P. Newman in the MWL@MIT.\n"
                      << "Licensed under the GPL3.0.\n";
            return 0;
        }
        
        if (!variable_map.count("type")) {
            printUsage(visible_options);
            std::cerr << oat::Error("A TYPE name must be specified. Exiting.\n");
            return -1;
        }

        if (!variable_map.count("sink")) {
            printUsage(visible_options);
            std::cerr << oat::Error("A SINK name must be specified. Exiting.\n");
            return -1;
        }

        if ((variable_map.count("config-file") && !variable_map.count("config-key")) ||
                (!variable_map.count("config-file") && variable_map.count("config-key"))) {
            printUsage(visible_options);
            std::cerr << oat::Error("A config file must be supplied with a corresponding config-key.\n");
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
    
    // TODO: Right now I need to use oat::Position2D as a template parameter
    // because otherwise this is no longer a valid base class for RandomAccel2D whose
    // base class is indeed TestPosition<oat::Position2D>

    // Create component
    std::shared_ptr<TestPosition<oat::Position2D>> test_position;

    // Refine component type
    switch (type_hash[type]) {
        case 'a':
        {
            test_position =  std::make_shared<RandomAccel2D>(sink, samples_per_second);
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
            test_position->configure(config_file, config_key);

        // Tell user
        std::cout << oat::whoMessage(test_position->get_name(),
                "Steaming to sink " + oat::sinkText(sink) + ".\n")
                << oat::whoMessage(test_position->get_name(),
                "Press CTRL+C to exit.\n");

        // Infinite loop until ctrl-c or end of stream signal
        run(test_position);

        // Tell user
        std::cout << oat::whoMessage(test_position->get_name(), "Exiting.\n");

        // Exit
        return 0;

    } catch (const cpptoml::parse_exception& ex) {
        std::cerr << oat::whoError(test_position->get_name(), "Failed to parse configuration file " + config_file + "\n")
                  << oat::whoError(test_position->get_name(), ex.what())
                  << "\n";
    } catch (const std::runtime_error ex) {
        std::cerr << oat::whoError(test_position->get_name(), ex.what())
                  << "\n";
    } catch (const cv::Exception ex) {
        std::cerr << oat::whoError(test_position->get_name(), ex.msg)
                  << "\n";
    } catch (...) {
        std::cerr << oat::whoError(test_position->get_name(), "Unknown exception.\n");
    }

    // Exit failure
    return -1;
}


