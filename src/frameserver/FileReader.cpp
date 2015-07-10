//******************************************************************************
//* Copyright (c) Jon Newman (jpnewman at mit snail edu) 
//* All right reserved.
//* This file is part of the Simple Tracker project.
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

#include <chrono>
#include <string>
#include <thread>
#include <opencv2/videoio.hpp>

#include "../../lib/cpptoml/cpptoml.h"
#include "../../lib/utility/IOFormat.h"

#include "FileReader.h"

FileReader::FileReader(std::string file_name_in, std::string image_sink_name, const double frames_per_second) :
  FrameServer(image_sink_name)
, file_name(file_name_in)
, file_reader(file_name_in)
, use_roi(false)
, frame_rate_in_hz(frames_per_second) {

    // Default config
    configure();
    tick = clock.now();
}

void FileReader::grabFrame(cv::Mat& frame) {
    
    file_reader >> frame;
    
    // Crop if necessary
    if (use_roi) {
        frame = frame(region_of_interest);
    }
    
    auto tock = clock.now();
    std::this_thread::sleep_for(frame_period_in_sec - (tock - tick));
    
    tick = clock.now();
}

void FileReader::configure() {
    calculateFramePeriod();
}

void FileReader::configure(const std::string& config_file, const std::string& config_key) {

    // This will throw cpptoml::parse_exception if a file 
    // with invalid TOML is provided
    cpptoml::table config;
    config = cpptoml::parse_file(config_file);

    // See if a camera configuration was provided
    if (config.contains(config_key)) {

        auto this_config = *config.get_table(config_key);

        if (this_config.contains("frame_rate")) {
            frame_rate_in_hz = (double) (*this_config.get_as<double>("frame_rate"));
            calculateFramePeriod();
        }

        if (this_config.contains("roi")) {

            auto roi = *this_config.get_table("roi");

            region_of_interest.x = (int) (*roi.get_as<int64_t>("x_offset"));
            region_of_interest.y = (int) (*roi.get_as<int64_t>("y_offset"));
            region_of_interest.width = (int) (*roi.get_as<int64_t>("width"));
            region_of_interest.height = (int) (*roi.get_as<int64_t>("height"));

            use_roi = true;

        } else {

            use_roi = false;
        }

        if (this_config.contains("calibration_file")) {

            std::string calibration_file = (*this_config.get_as<std::string>("calibration_file"));

            cv::FileStorage fs;
            fs.open(calibration_file, cv::FileStorage::READ);

            if (!fs.isOpened()) {
                std::cerr << "Failed to open " << calibration_file << std::endl;
                exit(EXIT_FAILURE);
            }

            // TODO: Exception handling for missing entries
            // Get calibration info
            fs["calibration_valid"] >> undistort_image;
            fs["camera_matrix"] >> camera_matrix;
            fs["distortion_coefficients"] >> distortion_coefficients;

            fs.release();
        }
    } else {
        throw (std::runtime_error(oat::configNoTableError(config_key, config_file)));
    }
}

void FileReader::calculateFramePeriod() {
    
    std::chrono::duration<double> frame_period{1.0 / frame_rate_in_hz};

    // Automatic conversion
    frame_period_in_sec = frame_period;
}
