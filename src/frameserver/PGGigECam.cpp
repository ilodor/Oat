//******************************************************************************
//* File:   PGGigECam.cpp 
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
//******************************************************************************

#include "PGGigECam.h"

#include <string>
#include <stdlib.h> 
#include <unistd.h> // TODO: Don't know how to include only if LINUX with cmake
#include <stdint.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <opencv2/opencv.hpp>

#include "FlyCapture2.h"

#include "../../lib/cpptoml/cpptoml.h"
#include "../../lib/cpptoml/OatTOMLSanitize.h"
#include "../../lib/utility/IOFormat.h"

#include "stdafx.h"

using namespace FlyCapture2;

PGGigECam::PGGigECam(std::string frame_sink_name, size_t index) : FrameServer(frame_sink_name)
, num_cameras(0)
, max_index (0)
, index_(index)
, x_bin(1)
, y_bin(1)
, gain_dB(0)
, shutter_ms(0)
, exposure_EV(0)
, aquisition_started(false)
, use_trigger(false)
, use_software_trigger(false)
, trigger_polarity(true)
, trigger_mode(14)
, trigger_source_pin(0)
, frames_per_second(30)
, use_camera_frame_buffer(false) { 

    // Find the number of cameras on the bus
    findNumCameras();
}

/**
 * Set default camera configuration
 */
void PGGigECam::configure() {

    setCameraIndex(index_);
    connectToCamera();
    turnCameraOn();
    setupStreamChannels();
    setupFrameRate(frames_per_second, true);
    setupExposure(true);
    setupShutter(true);
    setupGain(true);
    setupWhiteBalance(false);
    setupDefaultImageFormat();
    setupTrigger();
    setupCameraFrameBuffer();
}

/**
 * Configure file using TOML configuration file
 * @param config_file The configuration file.
 * @param key The configuration key specifying options for this instance.
 */
void PGGigECam::configure(const std::string& config_file, const std::string& config_key) {

    // Available options
    std::vector<std::string> options {"index", 
                                      "fps",
                                      "exposure", 
                                      "shutter", 
                                      "gain", 
                                      "white_bal", 
                                      "roi", 
                                      "x_bin",
                                      "y_bin",
                                      "trigger_on", 
                                      "trigger_rising", 
                                      "trigger_mode", 
                                      "trigger_pin", 
                                      "calibration_file" };
    
    // This will throw cpptoml::parse_exception if a file 
    // with invalid TOML is provided
    cpptoml::table config;
    config = cpptoml::parse_file(config_file);

    // See if a camera configuration was provided
    if (config.contains(config_key)) {

        // Get this components configuration table
        auto this_config = config.get_table(config_key);
        
        // Check for unknown options in the table and throw if you find them
        oat::config::checkKeys(options, this_config);

        // Camera index
        {
            int64_t val;
            if (oat::config::getValue(this_config, "index", val, min_index, max_index)) {
                index_ = val;
            }
        }
        
        setCameraIndex(index_);
        connectToCamera();
        turnCameraOn();
        setupStreamChannels();
        
        // Frame rate
        if (oat::config::getValue(this_config, "fps", frames_per_second, 0.0))
            setupFrameRate(frames_per_second, false);
        else
            setupFrameRate(frames_per_second, true);

        // Set the exposure
        {
            double val;
            if (oat::config::getValue(this_config, "exposure", val)) {
                exposure_EV = val;
                setupExposure(false);
            } else {
                setupExposure(true);
            }
        }

        // Set the shutter time
        {
            double val;
            if (!this_config->contains("exposure") && 
                oat::config::getValue(this_config, "shutter", val, 0.0, 1000.0)) {
                shutter_ms = val;
                setupShutter(false);
            } else {
                setupShutter(true);
            }
        }

        // Set the gain
        {
            double val;
            if (!this_config->contains("exposure") && 
                oat::config::getValue(this_config, "gain", val)) {
                gain_dB = val;
                setupGain(false);
            } else {
                setupGain(true);
            }
        }
        
        // Set white balance
        {
            oat::config::Table wb;
            if (oat::config::getTable(this_config, "white_bal", wb)) {
                oat::config::getValue(wb, "red", white_bal_red, (int64_t)0, (int64_t)1000, true);
                oat::config::getValue(wb, "blue", white_bal_blue , (int64_t)0, (int64_t)1000, true);
            } else {
                setupWhiteBalance(false);
            }
        }
        
        // Pixel binning
        {
            int64_t val;
            if (oat::config::getValue(this_config, "x_bin", val, (int64_t)0, (int64_t)8)) {
                x_bin = val;
            }
            
            if (oat::config::getValue(this_config, "y_bin", val, (int64_t)0, (int64_t)8)) {
                y_bin = val;
            }
        }
        
        setupPixelBinning();

        // Set the ROI
        // TODO: Use the base class's included region_of_interest property instead of frame_offset
        // and frame_size
        {
            oat::config::Table roi;
            if (oat::config::getTable(this_config, "roi", roi)) {

                int64_t val;
                oat::config::getValue(roi, "x_offset", val, (int64_t)0, true);
                region_of_interest.x = val;
                oat::config::getValue(roi, "y_offset", val, (int64_t)0, true);
                region_of_interest.y = val;
                oat::config::getValue(roi, "width", val, (int64_t)0, true);
                region_of_interest.width = val;
                oat::config::getValue(roi, "height", val, (int64_t)0, true);
                region_of_interest.height = val;
                setupImageFormat();
            } else {
                setupDefaultImageFormat();
            }
        }
        
        // Setup trigger
        if (oat::config::getValue(this_config, "trigger_on", use_trigger)) {

            oat::config::getValue(this_config, "trigger_rising", trigger_polarity);

            if (!oat::config::getValue(this_config, "trigger_mode", trigger_mode))
                trigger_mode = 14;

            if (trigger_mode == 7)
                use_software_trigger = true;
            else
                use_software_trigger = false;

            if (!oat::config::getValue(this_config, "trigger_source", trigger_source_pin))
                trigger_source_pin = 0;
        }

        setupTrigger();

        // TODO: Exception handling for missing entries
        // Get calibration info
        // TODO: use standard TOML format for these matracies instead 
        // of the secondary YML config file
        std::string calibration_file;
        if (oat::config::getValue(this_config, "calibration_file", calibration_file)) {

            cv::FileStorage fs;
            fs.open(calibration_file, cv::FileStorage::READ);

            if (!fs.isOpened()) {
                throw (std::runtime_error("Failed to open calibration file " + calibration_file));
            }

            fs["calibration_valid"] >> undistort_image;
            fs["camera_matrix"] >> camera_matrix;
            fs["distortion_coefficients"] >> distortion_coefficients;

            fs.release();
        }

//        if (this_config->contains("retry")) {
//
//            number_transmit_retries = *this_config->get_as<int64_t>("retry");
//
//            if (number_transmit_retries > 0) {
//                use_camera_frame_buffer = true;
//            } else {
//                use_camera_frame_buffer = false;
//            }
//        }
//
//        setupCameraFrameBuffer();

    } else {
        throw (std::runtime_error(oat::configNoTableError(config_key, config_file)));
    }

}

int PGGigECam::setCameraIndex(unsigned int requested_idx) {

    // Print bus information and find the number of cameras on the bus
    printBusInfo();

    if (num_cameras > requested_idx) {
        index_ = requested_idx;
    } else {
        throw (std::runtime_error("Requested camera index " + 
                std::to_string(requested_idx) + " is out of range.\n"));
    }

    return 0;
}

int PGGigECam::connectToCamera(void) {

    std::cout << "Connecting to camera: " << index_ << "\n";

    BusManager busMgr;
    PGRGuid guid;
    Error error = busMgr.GetCameraFromIndex(index_, &guid);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    // Connect to a camera
    error = camera.Connect(&guid);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    // Print camera information
    printCameraInfo();

    return 0;
}

int PGGigECam::setupStreamChannels() {

    unsigned int numStreamChannels = 0;
    Error error = camera.GetNumStreamChannels(&numStreamChannels);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    for (unsigned int i = 0; i < numStreamChannels; i++) {
        GigEStreamChannel streamChannel;
        error = camera.GetGigEStreamChannelInfo(i, &streamChannel);
        if (error != PGRERROR_OK) {
            throw (std::runtime_error(error.GetDescription()));
        }

        // TODO: This is not going to be valid if cameras are on a switch...
        streamChannel.destinationIpAddress.octets[0] = 224;
        streamChannel.destinationIpAddress.octets[1] = 0;
        streamChannel.destinationIpAddress.octets[2] = 0;
        streamChannel.destinationIpAddress.octets[3] = 1;

        // TODO: Make a more reasoned choice for these parameters based on the
        // number of cameras on the system...
        streamChannel.packetSize = 9000;
        streamChannel.interPacketDelay = 1000;

        error = camera.SetGigEStreamChannelInfo(i, &streamChannel);
        if (error != PGRERROR_OK) {
            throw (std::runtime_error(error.GetDescription()));
        }

        std::cout << "Printing stream channel information for channel " << i << "\n";
        printStreamChannelInfo(&streamChannel);
    }

    return 0;
}

int PGGigECam::setupFrameRate(double fps, bool is_auto) {
    
    std::cout << "Setting up frame rate...\n";
    
    Property prop;
    prop.type = FRAME_RATE;
    Error error = camera.GetProperty(&prop);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }
    
    prop.autoManualMode = is_auto;
    
    if (!is_auto) {
        prop.absValue = fps;
        std::cout << "Frame rate set to " + std::to_string(prop.absValue) + " FPS.\n";
    } else {
        std::cout << "Frame rate set to auto.\n";
    }
    
    error = camera.SetProperty(&prop);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }
    
}

int PGGigECam::setupShutter(bool is_auto) {

    std::cout << "Setting up shutter...\n";

    Property prop;
    prop.type = SHUTTER;
    Error error = camera.GetProperty(&prop);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    prop.autoManualMode = is_auto;
    prop.absControl = true;
    prop.absValue = shutter_ms;

    error = camera.SetProperty(&prop);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    if (is_auto) {
        std::cout << "Shutter set to auto.\n";
    } else {
        std::cout << "Shutter time set to " << std::fixed << std::setprecision(2) << shutter_ms << " ms.\n";
    }

    return 0;
}

int PGGigECam::setupShutter(float shutter_ms_in) {

    shutter_ms = shutter_ms_in;
    setupShutter(false);
}

int PGGigECam::setupGain(bool is_auto) {

    std::cout << "Setting camera gain...\n";

    Property prop;
    prop.type = GAIN;
    Error error = camera.GetProperty(&prop);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    prop.autoManualMode = is_auto;
    prop.absControl = true;
    prop.absValue = gain_dB;

    error = camera.SetProperty(&prop);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    if (is_auto) {
        std::cout << "Gain set to auto.\n";
    } else {
        std::cout << "Gain set to " << std::fixed << std::setprecision(2) << gain_dB << " dB.\n";
    }

    return 0;
}

int PGGigECam::setupGain(float gain_dB_in) {

    gain_dB = gain_dB_in;
    setupGain(false);

}

int PGGigECam::setupExposure(bool is_auto) {

    std::cout << "Setting up exposure...\n";

    setupShutter(true);
    setupGain(true);
    Property prop;
    prop.type = AUTO_EXPOSURE;
    Error error = camera.GetProperty(&prop);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    prop.onOff = true;
    prop.autoManualMode = is_auto;
    prop.absControl = true;
    prop.absValue = exposure_EV;

    error = camera.SetProperty(&prop);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    if (is_auto) {
        std::cout << "Exposure set to auto.\n";
    } else {
        std::cout << "Exposure set to " << std::fixed << std::setprecision(2) << exposure_EV << " EV.\n";
    }

    return 0;
}

int PGGigECam::setupExposure(float exposure_EV_in) {

    exposure_EV = exposure_EV_in;
    setupExposure(false);
}

int PGGigECam::setupWhiteBalance(bool is_on) {

    std::cout << "Setting camera white balance...\n";

    Property prop;
    prop.type = WHITE_BALANCE;
    Error error = camera.GetProperty(&prop);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    prop.onOff = is_on;
    prop.autoManualMode = false;
    prop.absControl = false;
    prop.valueA = white_bal_red;
    prop.valueB = white_bal_blue;

    error = camera.SetProperty(&prop);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    if (is_on) {
        std::cout << "White balance set to: \n";
        std::cout << "\tRed: " << std::fixed << std::setprecision(2) << white_bal_red << "\n";
        std::cout << "\tBlue: " << std::fixed << std::setprecision(2) << white_bal_blue << "\n";
    } else {
        std::cout << "White balance turned off.\n";
    }

    return 0;
}

int PGGigECam::setupWhiteBalance(int white_bal_red_in, int white_bal_blue_in) {

    white_bal_red = white_bal_red_in;
    white_bal_blue = white_bal_blue_in;
    setupWhiteBalance(true);

}

/**
 * Default image setup. Image uses all available pixels and BRG pixel format. 
 * 
 * @return 0 if successful.
 */
int PGGigECam::setupDefaultImageFormat() {

    std::cout << "Querying GigE image setting information...\n";

    Error error = camera.GetGigEImageSettingsInfo(&image_settings_info);
    if (error != PGRERROR_OK) {
        printError(error);
        return -1;
    }

    region_of_interest.x = 0;
    region_of_interest.y = 0;
    region_of_interest.width = image_settings_info.maxWidth;
    region_of_interest.height = image_settings_info.maxHeight;

    GigEImageSettings imageSettings;
    imageSettings.offsetX = region_of_interest.x;
    imageSettings.offsetY = region_of_interest.y;
    imageSettings.height = region_of_interest.height;
    imageSettings.width = region_of_interest.width;
    imageSettings.pixelFormat = PIXEL_FORMAT_RAW12;

    std::cout << "Setting GigE image settings...\n";

    error = camera.SetGigEImageSettings(&imageSettings);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    return 0;

}

/**
 * Custom image setup. Image uses the internally specified ROI settings.
 * @return 
 */
int PGGigECam::setupImageFormat() {

    std::cout << "Querying GigE image setting information...\n";

    Error error = camera.GetGigEImageSettingsInfo(&image_settings_info);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    if (region_of_interest.x > image_settings_info.maxWidth ||
            region_of_interest.y > image_settings_info.maxHeight) {
        throw (std::runtime_error("ROI pixel offsets are larger than the CCD array. Exiting.\n"));
    }

    if ((region_of_interest.width + region_of_interest.x) > image_settings_info.maxWidth) {
        throw (std::runtime_error("Current X-axis ROI settings are off the CCD array\n"));
    }

    if ((region_of_interest.height + region_of_interest.y) > image_settings_info.maxHeight) {
        throw (std::runtime_error("Current Y-axis ROI settings are off the CCD array\n"));
    }

    GigEImageSettings imageSettings;
    imageSettings.offsetX = region_of_interest.x;
    imageSettings.offsetY = region_of_interest.y;
    imageSettings.height = region_of_interest.height;
    imageSettings.width = region_of_interest.width;
    imageSettings.pixelFormat = PIXEL_FORMAT_RAW12;

    std::cout << "Setting GigE image settings...\n";

    error = camera.SetGigEImageSettings(&imageSettings);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    return 0;
}

int PGGigECam::setupPixelBinning() {  
    
    // On-board image binning
    Error error;
    error = camera.SetGigEImageBinningSettings(x_bin, y_bin);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }
    
    return 0;
}

int PGGigECam::setupCameraFrameBuffer() {

    // If requested
    if (use_camera_frame_buffer) {

        unsigned int image_retransmit_reg;
        const unsigned int image_retransmit_addr = 0x634;

        Error error = camera.ReadRegister(image_retransmit_addr, &image_retransmit_reg);
        if (error != PGRERROR_OK) {
            throw (std::runtime_error(error.GetDescription()));
        }

        std::cout << "Setting up camera frame buffering...\n";
        // Enable framebuffer
        image_retransmit_reg |= 1 << 0;

        // Direct image data through frame buffer
        image_retransmit_reg |= 1 << 1;

        error = camera.WriteRegister(image_retransmit_addr, image_retransmit_reg);
        if (error != PGRERROR_OK) {
            std::cout << "Warning: camera frame buffering requested, but this camera does not support frame buffering.\n"
                    << "Request ignored.\n";
            use_camera_frame_buffer = false;
        }
    }
}

/**
 * Once connected to the camera, issue power on command.
 * 
 * @return 0 if successful.
 */
int PGGigECam::turnCameraOn() {

    // Power on the camera
    const unsigned int k_cameraPower = 0x610;
    const unsigned int k_powerVal = 0x80000000;
    Error error = camera.WriteRegister(k_cameraPower, k_powerVal);
    if (error != PGRERROR_OK && error != PGRERROR_NOT_IMPLEMENTED){
        throw (std::runtime_error(error.GetDescription()));
    }

    const unsigned int millisecondsToSleep = 100;
    unsigned int regVal = 0;
    unsigned int retries = 10;

    // Wait for camera to complete power-up
    do {
#if defined(WIN32) || defined(WIN64)
        Sleep(millisecondsToSleep);
#else
        usleep(millisecondsToSleep * 1000);
#endif
        error = camera.ReadRegister(k_cameraPower, &regVal);
        if (error == PGRERROR_TIMEOUT) {
            // ignore timeout errors, camera may not be responding to
            // register reads during power-up
        } else if (error != PGRERROR_OK) {
            throw (std::runtime_error(error.GetDescription()));
        }

        retries--;
    } while ((regVal & k_powerVal) == 0 && retries > 0);

    // Check for timeout errors after retrying
    if (error == PGRERROR_TIMEOUT) {
        throw (std::runtime_error(error.GetDescription()));
    }

    return 0;
}

int PGGigECam::setupTrigger() {

    // Get current trigger settings
    Error error = camera.GetTriggerModeInfo(&trigger_mode_info);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    if (trigger_mode_info.present != true) {
        throw (std::runtime_error(error.GetDescription()));
    }

    TriggerMode triggerMode;
    error = camera.GetTriggerMode(&triggerMode);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    triggerMode.onOff = use_trigger;
    triggerMode.polarity = trigger_polarity;
    triggerMode.mode = trigger_mode;
    triggerMode.parameter = 0;
    triggerMode.source = trigger_source_pin;

    error = camera.SetTriggerMode(&triggerMode);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    // Setup frame buffering
    FC2Config flyCapConfig;
    error = camera.GetConfiguration(&flyCapConfig);
    flyCapConfig.grabMode = DROP_FRAMES; // TODO: buffering??
    flyCapConfig.highPerformanceRetrieveBuffer = true;
    flyCapConfig.numBuffers = 10;

    error = camera.SetConfiguration(&flyCapConfig);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    //TODO: Custom frame rate
//    VideoMode videoMode;
//    FrameRate frameRate;
//    camera.GetVideoModeAndFrameRate ( &videoMode, &frameRate);

    // TODO: This hangs...
    //Poll to ensure camera is ready
    //    if (use_trigger) { // If false, camera will free run
    //        bool retVal = pollForTriggerReady();
    //        if (!retVal) {
    //            std::cout << "\n";
    //            std::cout << "Error polling for trigger ready. Exiting...\n";
    //            exit(EXIT_FAILURE);
    //        }
    //    }

    // Camera is ready, start capturing images
    error = camera.StartCapture();
    if (error == PGRERROR_ISOCH_BANDWIDTH_EXCEEDED) {
        throw (std::runtime_error("Interface bandwidth exceeded. Cannot start camera..\n"));
    } else if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    aquisition_started = true;
    if (use_trigger && use_software_trigger) {
        std::cout << "Trigger the camera by pressing Enter\n";
    } else if (use_trigger) {
        std::cout << "Trigger the camera by sending a trigger pulse to GPIO_" << triggerMode.source << "\n";
    } else {
        std::cout << "Camera by started in free running mode.\n";
    }

    return 0;
}

// TODO: event driven acquisition.
//void PGGigECam::onGrabbedImage(Image* pImage, const void* pCallbackData) {
//    
//    &raw_image = pImage;
//    current_frame = imageToMat();
//    serveMat();
//    
//}

void PGGigECam::grabImage() {

    // Get the image
    if (!aquisition_started) {
        throw (std::runtime_error("Cannot grab image because acquisition has not been started.\n"));
    }

    Error error = camera.RetrieveBuffer(&raw_image);
    if (error == PGRERROR_IMAGE_CONSISTENCY_ERROR) {
        std::cerr << "WARNING: torn image detected.\n";

        // TODO: implement onboard buffer and perform retry a RetrieveBuffer
        // A single time if a torn image is detected.
    } else if (error != PGRERROR_OK) {
        printError(error);
        std::cerr << "WARNING: capture error.\n";
    }
}

cv::Mat PGGigECam::imageToMat() {

    // convert to rgb
    raw_image.Convert(FlyCapture2::PIXEL_FORMAT_BGR, &rgb_image);

    // convert to OpenCV cv::Mat
    unsigned int rowBytes = (double) rgb_image.GetReceivedDataSize() / (double) rgb_image.GetRows();
    return cv::Mat(rgb_image.GetRows(), rgb_image.GetCols(), CV_8UC3, rgb_image.GetData(), rowBytes);

}

void PGGigECam::grabFrame(cv::Mat& frame) {

    grabImage();
    frame = imageToMat();
}

// PRIVATE

int PGGigECam::findNumCameras(void) {

    Error error;
    BusManager busMgr;

    error = busMgr.GetNumOfCameras(&num_cameras);
    if (num_cameras == 0) {
        throw (std::runtime_error("No GigE cameras were detected.\n"));
    }
    
    max_index = num_cameras - 1;
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    return 0;
}

void PGGigECam::printError(Error error) {
    error.PrintErrorTrace();
    if (!camera.IsConnected()) {
        std::cerr << "Camera must be connected before getting its info.\n";
    }
}

bool PGGigECam::pollForTriggerReady() {

    const unsigned int k_softwareTrigger = 0x62C;
    Error error;
    unsigned int regVal = 0;

    do {
        error = camera.ReadRegister(k_softwareTrigger, &regVal);
        if (error != PGRERROR_OK) {
            throw (std::runtime_error(error.GetDescription()));
        }

    } while ((regVal >> 31) != 0);

    return true;
}

int PGGigECam::printBusInfo(void) {

    std::cout << "\n";
    std::cout << "*** BUS INFORMATION ***\n";
    std::cout << "No. cameras detected on bus: " << num_cameras << "\n";
    return 0;
}

int PGGigECam::printCameraInfo(void) {

    Error error = camera.GetCameraInfo(&camera_info);
    if (error != PGRERROR_OK) {
        throw (std::runtime_error(error.GetDescription()));
    }

    std::ostringstream macAddress;
    macAddress << std::hex << std::setw(2) << std::setfill('0') << (unsigned int) camera_info.macAddress.octets[0] << ":" <<
            std::hex << std::setw(2) << std::setfill('0') << (unsigned int) camera_info.macAddress.octets[1] << ":" <<
            std::hex << std::setw(2) << std::setfill('0') << (unsigned int) camera_info.macAddress.octets[2] << ":" <<
            std::hex << std::setw(2) << std::setfill('0') << (unsigned int) camera_info.macAddress.octets[3] << ":" <<
            std::hex << std::setw(2) << std::setfill('0') << (unsigned int) camera_info.macAddress.octets[4] << ":" <<
            std::hex << std::setw(2) << std::setfill('0') << (unsigned int) camera_info.macAddress.octets[5];


    std::ostringstream ipAddress;
    ipAddress << (unsigned int) camera_info.ipAddress.octets[0] << "." <<
            (unsigned int) camera_info.ipAddress.octets[1] << "." <<
            (unsigned int) camera_info.ipAddress.octets[2] << "." <<
            (unsigned int) camera_info.ipAddress.octets[3];

    std::ostringstream subnetMask;
    subnetMask << (unsigned int) camera_info.subnetMask.octets[0] << "." <<
            (unsigned int) camera_info.subnetMask.octets[1] << "." <<
            (unsigned int) camera_info.subnetMask.octets[2] << "." <<
            (unsigned int) camera_info.subnetMask.octets[3];

    std::ostringstream defaultGateway;
    defaultGateway << (unsigned int) camera_info.defaultGateway.octets[0] << "." <<
            (unsigned int) camera_info.defaultGateway.octets[1] << "." <<
            (unsigned int) camera_info.defaultGateway.octets[2] << "." <<
            (unsigned int) camera_info.defaultGateway.octets[3];

    std::cout << "\n";
    std::cout << "*** GENERAL CAMERA INFORMATION ***\n";
    std::cout << "Serial number: " << camera_info.serialNumber << "\n";
    std::cout << "Camera model: " << camera_info.modelName << "\n";
    std::cout << "Camera vendor: " << camera_info.vendorName << "\n";
    std::cout << "Sensor: " << camera_info.sensorInfo << "\n";
    std::cout << "Resolution: " << camera_info.sensorResolution << "\n";
    std::cout << "Firmware version: " << camera_info.firmwareVersion << "\n";
    std::cout << "Firmware build time: " << camera_info.firmwareBuildTime << "\n\n";

    std::cout << "*** CAMERA INTERFACE INFORMATION ***\n";
    std::cout << "GigE version :" << camera_info.gigEMajorVersion << "." << camera_info.gigEMinorVersion << "\n";
    std::cout << "User defined name :" << camera_info.userDefinedName << "\n";
    std::cout << "XML URL 1: " << camera_info.xmlURL1 << "\n";
    std::cout << "XML URL 2: " << camera_info.xmlURL2 << "\n";
    std::cout << "MAC address: " << macAddress.str() << "\n";
    std::cout << "IP address: " << ipAddress.str() << "\n";
    std::cout << "Subnet mask: " << subnetMask.str() << "\n";
    std::cout << "Default gateway: " << defaultGateway.str() << "\n\n";


    return 0;
}

void PGGigECam::printStreamChannelInfo(GigEStreamChannel *pStreamChannel) {
    //char ipAddress[32];
    std::ostringstream ipAddress;
    ipAddress << (unsigned int) pStreamChannel->destinationIpAddress.octets[0] << "." <<
            (unsigned int) pStreamChannel->destinationIpAddress.octets[1] << "." <<
            (unsigned int) pStreamChannel->destinationIpAddress.octets[2] << "." <<
            (unsigned int) pStreamChannel->destinationIpAddress.octets[3];

    std::cout << "Network interface: " << pStreamChannel->networkInterfaceIndex << "\n";
    //std::cout << "Host Port: " << pStreamChannel->hostPort << "\n";
    std::cout << "Do not fragment bit: " << (pStreamChannel->doNotFragment ? "Enabled" : "Disabled") << "\n";
    std::cout << "Packet size: " << pStreamChannel->packetSize << "\n";
    std::cout << "Inter packet delay: " << pStreamChannel->interPacketDelay << "\n";
    std::cout << "Destination IP address: " << ipAddress.str() << "\n";
    std::cout << "Source port (on camera): " << pStreamChannel->sourcePort << "\n\n";

}

void PGGigECam::fireSoftwareTrigger() {
    // Check that the trigger is ready

    if (use_trigger && use_software_trigger) {

        pollForTriggerReady();

        std::cout << "Press the Enter key to initiate a software trigger\n";
        std::cin.ignore();

        // Fire software trigger
        const unsigned int k_softwareTrigger = 0x62C;
        const unsigned int k_fireVal = 0x80000000;
        Error error;

        error = camera.WriteRegister(k_softwareTrigger, k_fireVal);
        if (error != PGRERROR_OK) {
            printError(error);
            std::cout << "Error firing software trigger\n";
        }

    } else {

        std::cout << "Cannot firing software trigger because software trigger has not been configured.\n";
    }
}
