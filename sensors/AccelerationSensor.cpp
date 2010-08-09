/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>

#include <linux/kxtf9.h>

#include <cutils/log.h>

#include "AccelerationSensor.h"

/*****************************************************************************/

AccelerationSensor::AccelerationSensor()
    : SensorBase(ACCELEROMETER_DEVICE_NAME, "accelerometer"),
      mEnabled(0),
      mOrientationEnabled(0),
      mInputReader(32)
{
    mPendingEvent.version = sizeof(sensors_event_t);
    mPendingEvent.sensor = ID_A;
    mPendingEvent.type = SENSOR_TYPE_ACCELEROMETER;
    mPendingEvent.acceleration.status = SENSOR_STATUS_ACCURACY_HIGH;
    memset(mPendingEvent.data, 0x00, sizeof(mPendingEvent.data));

    // read the actual value of all sensors if they're enabled already
    struct input_absinfo absinfo;
    int flags = 0;
    if (!ioctl(dev_fd, KXTF9_IOCTL_GET_ENABLE, &flags)) {
	LOGE("%s: AccelerationSensor GET Enable Flag =%d", __PRETTY_FUNCTION__, flags);
        if (flags)  {
            mEnabled = 1;
            if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_X), &absinfo)) {
                mPendingEvent.acceleration.x = absinfo.value * CONVERT_A_X;
            }
            if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_Y), &absinfo)) {
                mPendingEvent.acceleration.y = absinfo.value * CONVERT_A_Y;
            }
            if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_Z), &absinfo)) {
                mPendingEvent.acceleration.z = absinfo.value * CONVERT_A_Z;
            }
        }
    }
}

AccelerationSensor::~AccelerationSensor() {
}

int AccelerationSensor::enable(int32_t, int en)
{
    int flags = en ? 1 : 0;
    int err = 0;
    if (flags != mEnabled) {
		LOGE("%s: AccelerationSensor Enable Flag =%d", __PRETTY_FUNCTION__, flags);
        // don't turn the accelerometer off, if the orientation
        // sensor is enabled
        if (mOrientationEnabled && !en) {
            mEnabled = flags;
            return 0;
        }
        err = ioctl(dev_fd, KXTF9_IOCTL_SET_ENABLE, &flags);
        err = err<0 ? -errno : 0;
        LOGE_IF(err, "KXTF9_IOCTL_SET_ENABLE failed (%s)", strerror(-err));
        if (!err) {
            mEnabled = flags;
        }
    }
    return err;
}

int AccelerationSensor::enableOrientation(int en)
{
    int flags = en ? 1 : 0;
    int err = 0;
    if (flags != mOrientationEnabled) {
        // don't turn the accelerometer off, if the user has requested it
        if (mEnabled && !en) {
            mOrientationEnabled = flags;
            return 0;
        }
        err = ioctl(dev_fd, KXTF9_IOCTL_SET_ENABLE, &flags);
        err = err<0 ? -errno : 0;
        LOGE_IF(err, "KXTF9_IOCTL_SET_ENABLE failed (%s)", strerror(-err));
        if (!err) {
            mOrientationEnabled = flags;
        }
    }
    return err;
}

int AccelerationSensor::setDelay(int64_t ns)
{
    if (ns < 0)
        return -EINVAL;

    short delay = ns / 1000000;
    if (!ioctl(dev_fd, KXTF9_IOCTL_SET_DELAY, &delay)) {
        return -errno;
    }
    return 0;
}

int AccelerationSensor::readEvents(sensors_event_t* data, int count)
{
    if (count < 1)
        return -EINVAL;

    ssize_t n = mInputReader.fill(data_fd);
    if (n < 0)
        return n;
    int numEventReceived = 0;
    input_event const* event;

    while (count && mInputReader.readEvent(&event)) {
        int type = event->type;
        if (type == EV_ABS) {
            processEvent(event->code, event->value);
        } else if (type == EV_SYN) {
            int64_t time = timevalToNano(event->time);
            mPendingEvent.timestamp = time;
            if (mEnabled) {
                *data++ = mPendingEvent;
                count--;
                numEventReceived++;
            }
        } else {
            LOGE("AccelerationSensor: unknown event (type=%d, code=%d)",
                    type, event->code);
        }
        mInputReader.next();
    }

    return numEventReceived;
}

void AccelerationSensor::processEvent(int code, int value)
{
    switch (code) {
        case EVENT_TYPE_ACCEL_X:
            mPendingEvent.acceleration.x = value * CONVERT_A_X;
            break;
        case EVENT_TYPE_ACCEL_Y:
            mPendingEvent.acceleration.y = value * CONVERT_A_Y;
            break;
        case EVENT_TYPE_ACCEL_Z:
            mPendingEvent.acceleration.z = value * CONVERT_A_Z;
            break;
    }
}
