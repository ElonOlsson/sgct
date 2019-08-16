/*************************************************************************
Copyright (c) 2012-2015 Miroslav Andel
All rights reserved.

For conditions of distribution and use, see copyright notice in sgct.h 
*************************************************************************/

#ifndef __SGCT__TRACKING_MANAGER__H__
#define __SGCT__TRACKING_MANAGER__H__

#include <memory>
#include <set>
#include <thread>
#include <vector>

namespace sgct_core { class SGCTUser; }

namespace sgct {

class SGCTTracker;
class SGCTTrackingDevice;

/*!
Class that manages tracking systems
*/
class SGCTTrackingManager {
public:
    ~SGCTTrackingManager();
    
    void startSampling();

    /**
     * Update the user position if headtracking is used. This function is called from the
     * engine.
     */
    void updateTrackingDevices();
    void addTracker(std::string name);
    void addDeviceToCurrentTracker(std::string name);
    void addSensorToCurrentDevice(std::string address, int id);
    void addButtonsToCurrentDevice(std::string address, int numOfButtons);
    void addAnalogsToCurrentDevice(std::string address, int numOfAxes);
    
    int getNumberOfTrackers() const;
    int getNumberOfDevices() const;
    SGCTTrackingDevice* getHeadDevicePtr() const;

    SGCTTracker* getLastTrackerPtr() const;
    SGCTTracker* getTrackerPtr(size_t index) const;
    SGCTTracker* getTrackerPtr(const std::string& name) const;

    void setEnabled(bool state);
    void setSamplingTime(double t);
    double getSamplingTime() const;

    bool isRunning() const;

private:
    std::unique_ptr<std::thread> mSamplingThread;
    std::vector<std::unique_ptr<SGCTTracker>> mTrackers;
    std::set<std::string> mAddresses;
    double mSamplingTime = 0.0;
    bool mRunning = true;

    sgct_core::SGCTUser* mHeadUser = nullptr;
    SGCTTrackingDevice* mHead = nullptr;
    int mNumberOfDevices = 0;
};

} // namespace sgct

#endif // __SGCT__TRACKING_MANAGER__H__
