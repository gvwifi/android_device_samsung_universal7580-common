/*
 * Copyright (C) 2024 The LineageOS Project
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

/*
 * Comprehensive self-contained CameraMetadata shim for vendor camera HAL.
 * Uses only the VNDK-safe camera_metadata C API.
 * No framework headers (utils/Errors.h, String8.h, etc.) are used.
 */

#include <system/camera_metadata.h>
#include <log/log.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Status codes matching Android's utils/Errors.h */
typedef int32_t status_t;
enum {
    OK                = 0,
    NO_ERROR          = 0,
    NO_MEMORY         = -ENOMEM,
    INVALID_OPERATION = -ENOSYS,
    BAD_VALUE         = -EINVAL,
    NAME_NOT_FOUND    = -ENOENT,
};

namespace android {

class CameraMetadata {
public:
    CameraMetadata();
    CameraMetadata(size_t entryCapacity, size_t dataCapacity = 10);
    CameraMetadata(const CameraMetadata &other);
    CameraMetadata(CameraMetadata &&other);
    CameraMetadata(camera_metadata_t *buffer);
    ~CameraMetadata();

    CameraMetadata &operator=(const CameraMetadata &other);
    CameraMetadata &operator=(CameraMetadata &&other);
    CameraMetadata &operator=(const camera_metadata_t *buffer);

    bool isEmpty() const;
    int entryCount() const;
    status_t sort();

    const camera_metadata_t* getAndLock() const;
    status_t unlock(const camera_metadata_t *buffer) const;

    camera_metadata_t *release();
    void clear();

    void acquire(camera_metadata_t *buffer);
    void acquire(CameraMetadata &other);

    status_t append(const CameraMetadata &other);
    status_t append(const camera_metadata_t *other);

    camera_metadata_entry find(uint32_t tag);
    camera_metadata_ro_entry find(uint32_t tag) const;

    status_t update(uint32_t tag, const uint8_t *data, size_t data_count);
    status_t update(uint32_t tag, const int32_t *data, size_t data_count);
    status_t update(uint32_t tag, const float *data, size_t data_count);
    status_t update(uint32_t tag, const int64_t *data, size_t data_count);
    status_t update(uint32_t tag, const double *data, size_t data_count);
    status_t update(uint32_t tag, const camera_metadata_rational_t *data, size_t data_count);

    status_t erase(uint32_t tag);

    bool exists(uint32_t tag) const;

private:
    status_t updateImpl(uint32_t tag, const void *data, size_t data_count);
    status_t resizeIfNeeded(size_t extraEntries, size_t extraData);

    camera_metadata_t *mBuffer;
    mutable bool mLocked;
};

// ---- Constructors ----

CameraMetadata::CameraMetadata() :
        mBuffer(NULL), mLocked(false) {
}

CameraMetadata::CameraMetadata(size_t entryCapacity, size_t dataCapacity) :
        mLocked(false) {
    mBuffer = allocate_camera_metadata(entryCapacity, dataCapacity);
}

CameraMetadata::CameraMetadata(const CameraMetadata &other) :
        mLocked(false) {
    mBuffer = clone_camera_metadata(other.mBuffer);
}

CameraMetadata::CameraMetadata(CameraMetadata &&other) :
        mBuffer(other.mBuffer), mLocked(false) {
    other.mBuffer = NULL;
}

CameraMetadata::CameraMetadata(camera_metadata_t *buffer) :
        mBuffer(buffer), mLocked(false) {
}

// ---- Assignment operators ----

CameraMetadata &CameraMetadata::operator=(const CameraMetadata &other) {
    return operator=(other.mBuffer);
}

CameraMetadata &CameraMetadata::operator=(CameraMetadata &&other) {
    if (this != &other) {
        if (mLocked) {
            ALOGE("%s: Assignment to a locked CameraMetadata!", __FUNCTION__);
            return *this;
        }
        clear();
        mBuffer = other.mBuffer;
        other.mBuffer = NULL;
    }
    return *this;
}

CameraMetadata &CameraMetadata::operator=(const camera_metadata_t *buffer) {
    if (mLocked) {
        ALOGE("%s: Assignment to a locked CameraMetadata!", __FUNCTION__);
        return *this;
    }
    if (buffer != mBuffer) {
        camera_metadata_t *newBuffer = clone_camera_metadata(buffer);
        clear();
        mBuffer = newBuffer;
    }
    return *this;
}

// ---- Destructor ----

CameraMetadata::~CameraMetadata() {
    mLocked = false;
    clear();
}

// ---- Query methods ----

bool CameraMetadata::isEmpty() const {
    if (mBuffer == NULL) return true;
    return get_camera_metadata_entry_count(mBuffer) == 0;
}

int CameraMetadata::entryCount() const {
    if (mBuffer == NULL) return 0;
    return (int)get_camera_metadata_entry_count(mBuffer);
}

status_t CameraMetadata::sort() {
    if (mBuffer == NULL) return OK;
    return sort_camera_metadata(mBuffer);
}

bool CameraMetadata::exists(uint32_t tag) const {
    camera_metadata_ro_entry entry;
    return find_camera_metadata_ro_entry(mBuffer, tag, &entry) == 0;
}

// ---- Lock/Unlock ----

const camera_metadata_t* CameraMetadata::getAndLock() const {
    mLocked = true;
    return mBuffer;
}

status_t CameraMetadata::unlock(const camera_metadata_t *buffer) const {
    if (!mLocked) {
        ALOGE("%s: CameraMetadata is not locked!", __FUNCTION__);
        return INVALID_OPERATION;
    }
    if (buffer != mBuffer) {
        ALOGE("%s: Unlocking with wrong buffer pointer!", __FUNCTION__);
        return BAD_VALUE;
    }
    mLocked = false;
    return OK;
}

// ---- Release/Clear/Acquire ----

camera_metadata_t* CameraMetadata::release() {
    if (mLocked) {
        ALOGE("%s: CameraMetadata is locked", __FUNCTION__);
        return NULL;
    }
    camera_metadata_t *released = mBuffer;
    mBuffer = NULL;
    return released;
}

void CameraMetadata::clear() {
    if (mLocked) {
        ALOGE("%s: CameraMetadata is locked", __FUNCTION__);
        return;
    }
    if (mBuffer) {
        free_camera_metadata(mBuffer);
        mBuffer = NULL;
    }
}

void CameraMetadata::acquire(camera_metadata_t *buffer) {
    if (mLocked) {
        ALOGE("%s: CameraMetadata is locked", __FUNCTION__);
        return;
    }
    clear();
    mBuffer = buffer;
}

void CameraMetadata::acquire(CameraMetadata &other) {
    if (mLocked) {
        ALOGE("%s: CameraMetadata is locked", __FUNCTION__);
        return;
    }
    acquire(other.release());
}

// ---- Append ----

status_t CameraMetadata::append(const CameraMetadata &other) {
    return append(other.mBuffer);
}

status_t CameraMetadata::append(const camera_metadata_t *other) {
    if (other == NULL) return OK;
    size_t extraEntries = get_camera_metadata_entry_count(other);
    size_t extraData = get_camera_metadata_data_count(other);
    resizeIfNeeded(extraEntries, extraData);
    return append_camera_metadata(mBuffer, other);
}

// ---- Find ----

camera_metadata_entry CameraMetadata::find(uint32_t tag) {
    camera_metadata_entry entry;
    memset(&entry, 0, sizeof(entry));
    if (mBuffer == NULL) return entry;
    status_t res = find_camera_metadata_entry(mBuffer, tag, &entry);
    if (res != OK) {
        entry.count = 0;
        entry.data.u8 = NULL;
    }
    return entry;
}

camera_metadata_ro_entry CameraMetadata::find(uint32_t tag) const {
    camera_metadata_ro_entry entry;
    memset(&entry, 0, sizeof(entry));
    if (mBuffer == NULL) return entry;
    status_t res = find_camera_metadata_ro_entry(mBuffer, tag, &entry);
    if (res != OK) {
        entry.count = 0;
        entry.data.u8 = NULL;
    }
    return entry;
}

// ---- Erase ----

status_t CameraMetadata::erase(uint32_t tag) {
    camera_metadata_entry_t entry;
    status_t res = find_camera_metadata_entry(mBuffer, tag, &entry);
    if (res == NAME_NOT_FOUND) return OK;
    if (res != OK) return res;
    return delete_camera_metadata_entry(mBuffer, entry.index);
}

// ---- Update methods ----

status_t CameraMetadata::update(uint32_t tag,
        const uint8_t *data, size_t data_count) {
    if (mLocked) return INVALID_OPERATION;
    return updateImpl(tag, (const void*)data, data_count);
}

status_t CameraMetadata::update(uint32_t tag,
        const int32_t *data, size_t data_count) {
    if (mLocked) return INVALID_OPERATION;
    return updateImpl(tag, (const void*)data, data_count);
}

status_t CameraMetadata::update(uint32_t tag,
        const float *data, size_t data_count) {
    if (mLocked) return INVALID_OPERATION;
    return updateImpl(tag, (const void*)data, data_count);
}

status_t CameraMetadata::update(uint32_t tag,
        const int64_t *data, size_t data_count) {
    if (mLocked) return INVALID_OPERATION;
    return updateImpl(tag, (const void*)data, data_count);
}

status_t CameraMetadata::update(uint32_t tag,
        const double *data, size_t data_count) {
    if (mLocked) return INVALID_OPERATION;
    return updateImpl(tag, (const void*)data, data_count);
}

status_t CameraMetadata::update(uint32_t tag,
        const camera_metadata_rational_t *data, size_t data_count) {
    if (mLocked) return INVALID_OPERATION;
    return updateImpl(tag, (const void*)data, data_count);
}

// ---- Internal helpers ----

status_t CameraMetadata::updateImpl(uint32_t tag, const void *data,
        size_t data_count) {
    status_t res;
    int type = get_camera_metadata_tag_type(tag);
    if (type == -1) {
        ALOGE("%s: Tag %d not found", __FUNCTION__, tag);
        return BAD_VALUE;
    }
    size_t data_size = calculate_camera_metadata_entry_data_size(type,
            data_count);

    res = resizeIfNeeded(1, data_size);
    if (res != OK) {
        ALOGE("%s: Tag %d: Unable to resize metadata buffer", __FUNCTION__, tag);
        return res;
    }

    camera_metadata_entry_t entry;
    res = find_camera_metadata_entry(mBuffer, tag, &entry);
    if (res == NAME_NOT_FOUND) {
        res = add_camera_metadata_entry(mBuffer, tag, data, data_count);
    } else if (res == OK) {
        res = update_camera_metadata_entry(mBuffer, entry.index, data, data_count, NULL);
    }

    if (res != OK) {
        ALOGE("%s: Unable to update metadata entry %d", __FUNCTION__, tag);
    }
    return res;
}

status_t CameraMetadata::resizeIfNeeded(size_t extraEntries, size_t extraData) {
    if (mBuffer == NULL) {
        mBuffer = allocate_camera_metadata(extraEntries * 2, extraData * 2);
        if (mBuffer == NULL) {
            ALOGE("%s: Can't allocate metadata buffer", __FUNCTION__);
            return NO_MEMORY;
        }
    } else {
        size_t currentEntryCount = get_camera_metadata_entry_count(mBuffer);
        size_t currentEntryCap = get_camera_metadata_entry_capacity(mBuffer);
        size_t newEntryCount = currentEntryCount + extraEntries;
        newEntryCount = (newEntryCount > currentEntryCap) ?
                newEntryCount * 2 : currentEntryCap;

        size_t currentDataCount = get_camera_metadata_data_count(mBuffer);
        size_t currentDataCap = get_camera_metadata_data_capacity(mBuffer);
        size_t newDataCount = currentDataCount + extraData;
        newDataCount = (newDataCount > currentDataCap) ?
                newDataCount * 2 : currentDataCap;

        if (newEntryCount > currentEntryCap ||
                newDataCount > currentDataCap) {
            camera_metadata_t *oldBuffer = mBuffer;
            mBuffer = allocate_camera_metadata(newEntryCount, newDataCount);
            if (mBuffer == NULL) {
                ALOGE("%s: Can't allocate larger metadata buffer", __FUNCTION__);
                mBuffer = oldBuffer;
                return NO_MEMORY;
            }
            append_camera_metadata(mBuffer, oldBuffer);
            free_camera_metadata(oldBuffer);
        }
    }
    return OK;
}

}; // namespace android
