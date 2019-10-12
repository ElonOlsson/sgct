/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2019                                                               *
 * For conditions of distribution and use, see copyright notice in sgct.h                *
 ****************************************************************************************/

#include <sgct/shareddata.h>

#include <sgct/engine.h>
#include <sgct/messagehandler.h>
#include <sgct/mutexmanager.h>
#include <zlib.h>
#include <string>

namespace sgct {

SharedData* SharedData::_instance = nullptr;

SharedData* SharedData::instance() {
    if (_instance == nullptr) {
        _instance = new SharedData();
    }

    return _instance;
}

void SharedData::destroy() {
    if (_instance != nullptr) {
        delete _instance;
        _instance = nullptr;
    }
}

SharedData::SharedData() {
    constexpr const int DefaultSize = 1024;

    // use a compression buffer twice as large to fit huffman tree + data which can be
    // larger than original data in some cases. Normally a sixe x 1.1 should be enough.
    _compressedBuffer.resize(DefaultSize * 2);

    _dataBlock.reserve(DefaultSize);
    _dataBlockToCompress.reserve(DefaultSize);

    _compressionLevel = Z_BEST_SPEED;

    if (_useCompression) {
        _currentStorage = &_dataBlockToCompress;
    }
    else {
        _currentStorage = &_dataBlock;
    }

    // fill rest of header with Network::DefaultId
    memset(_headerSpace.data(), core::Network::DefaultId, core::Network::HeaderSize);

    _headerSpace[0] = core::Network::DataId;
}

void SharedData::setCompression(bool state, int level) {
    MutexManager::instance()->dataSyncMutex.lock();
    _useCompression = state;
    _compressionLevel = level;

    if (_useCompression) {
        _currentStorage = &_dataBlockToCompress;
    }
    else {
        _currentStorage = &_dataBlock;
        _compressionRatio = 1.f;
    }
    MutexManager::instance()->dataSyncMutex.unlock();
}

float SharedData::getCompressionRatio() {
    return _compressionRatio;
}

void SharedData::setEncodeFunction(void(*fnPtr)(void)) {
    _encodeFn = fnPtr;
}

void SharedData::setDecodeFunction(void(*fnPtr)(void)) {
    _decodeFn = fnPtr;
}

void SharedData::decode(const char* receivedData, int receivedLength, int) {
#ifdef __SGCT_NETWORK_DEBUG__
    MessageHandler::instance()->printDebug("SharedData::decode");
#endif
    MutexManager::instance()->dataSyncMutex.lock();

    // reset
    _pos = 0;
    _dataBlock.clear();

    if (receivedLength > static_cast<int>(_dataBlock.capacity())) {
        _dataBlock.reserve(receivedLength);
    }
    _dataBlock.insert(_dataBlock.end(), receivedData, receivedData + receivedLength);

    MutexManager::instance()->dataSyncMutex.unlock();

    if (_decodeFn != nullptr) {
        _decodeFn();
    }
}

void SharedData::encode() {
#ifdef __SGCT_NETWORK_DEBUG__
    MessageHandler::instance()->printDebug("SharedData::encode");
#endif
    MutexManager::instance()->dataSyncMutex.lock();

    _dataBlock.clear();
    if (_useCompression) {
        _dataBlockToCompress.clear();
        _headerSpace[0] = core::Network::CompressedDataId;
    }
    else {
        _headerSpace[0] = core::Network::DataId;
    }

    //reserve header space
    _dataBlock.insert(
        _dataBlock.begin(),
        _headerSpace.begin(),
        _headerSpace.begin() + core::Network::HeaderSize
    );

    MutexManager::instance()->dataSyncMutex.unlock();

    if (_encodeFn) {
        _encodeFn();
    }

    if (_useCompression && !_dataBlockToCompress.empty()) {
        // (abock, 2019-09-18); I was thinking of replacing this with a std::unique_lock
        // but the dataSyncMutex lock is unlocked further down *before* sending a message
        // to the MessageHandler which uses the dataSyncMutex internally.
        MutexManager::instance()->dataSyncMutex.lock();

        // re-allocate if needed use a compression buffer twice as large to fit huffman
        // tree + data which can  larger than original data in some cases.
        // Normally a sixe x 1.1 should be enough.
        if (_compressedBuffer.size() < (_dataBlockToCompress.size() / 2)) {
            _compressedBuffer.resize(_dataBlockToCompress.size() * 2);
        }

        uLongf compressedSize = static_cast<uLongf>(_compressedBuffer.size());
        uLongf dataSize = static_cast<uLongf>(_dataBlockToCompress.size());
        int err = compress2(
            _compressedBuffer.data(),
            &compressedSize,
            _dataBlockToCompress.data(),
            dataSize,
            _compressionLevel
        );

        if (err == Z_OK) {
            // add original size
            uint32_t uncompressedSize = static_cast<uint32_t>(_dataBlockToCompress.size());
            unsigned char* p = reinterpret_cast<unsigned char*>(&uncompressedSize);

            _dataBlock[9] = p[0];
            _dataBlock[10] = p[1];
            _dataBlock[11] = p[2];
            _dataBlock[12] = p[3];
            
            _compressionRatio = static_cast<float>(compressedSize) /
                                static_cast<float>(uncompressedSize);

            // add the compressed block
            _dataBlock.insert(
                _dataBlock.end(),
                _compressedBuffer.begin(),
                _compressedBuffer.begin() + compressedSize
            );
        }
        else {
            MutexManager::instance()->dataSyncMutex.unlock();
            MessageHandler::instance()->printError(
                "SharedData: Failed to compress data (error %d)", err
            );
            return;
        }

        MutexManager::instance()->dataSyncMutex.unlock();
    }
}

std::size_t SharedData::getUserDataSize() {
    return _dataBlock.size() - core::Network::HeaderSize;
}

unsigned char* SharedData::getDataBlock() {
    return _dataBlock.data();
}

size_t SharedData::getDataSize() {
    return _dataBlock.size();
}

size_t SharedData::getBufferSize() {
    return _dataBlock.capacity();
}

void SharedData::writeFloat(const SharedFloat& sf) {
#ifdef __SGCT_NETWORK_DEBUG__    
    MessageHandler::instance()->printDebug(
        MessageHandler::Level::Info,
        "SharedData::writeFloat\nFloat = %f", sf->getVal()
    );
#endif

    float val = sf.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    unsigned char* p = reinterpret_cast<unsigned char*>(&val);
    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(float));
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeDouble(const SharedDouble& sd) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug(
        "SharedData::writeDouble. Double = %f", sd->getVal()
    );
#endif

    double val = sd.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    unsigned char* p = reinterpret_cast<unsigned char*>(&val);
    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(double));
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeInt64(const SharedInt64& si) {
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug(
        "SharedData::writeInt64. Int = %ld", si->getVal()
    );
#endif

    int64_t val = si.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    unsigned char* p = reinterpret_cast<unsigned char* >(&val);
    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(int64_t));
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeInt32(const SharedInt32& si) {
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug(
        "SharedData::writeInt32. Int = %d", si->getVal()
    );
#endif

    int32_t val = si.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    unsigned char* p = reinterpret_cast<unsigned char*>(&val);
    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(int32_t));
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeInt16(const SharedInt16& si) {
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug(
        "SharedData::writeInt16. Int = %d", si->getVal()
    );
#endif

    int16_t val = si.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    unsigned char* p = reinterpret_cast<unsigned char*>(&val);
    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(int16_t));
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeInt8(const SharedInt8& si) {
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug(
        "SharedData::writeInt8. Int = %d", si->getVal()
    );
#endif

    int8_t val = si.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    unsigned char* p = reinterpret_cast<unsigned char*>(&val);
    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(int8_t));
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeUInt64(const SharedUInt64& si) {
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug(
        "SharedData::writeUInt64. UInt = %lu", si->getVal()
    );
#endif

    uint64_t val = si.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    unsigned char* p = reinterpret_cast<unsigned char*>(&val);
    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(uint64_t));
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeUInt32(const SharedUInt32& si) {
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug(
        "SharedData::writeUInt32. UInt = %u", si->getVal()
    );
#endif

    uint32_t val = si.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    unsigned char* p = reinterpret_cast<unsigned char*>(&val);
    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(uint32_t));
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeUInt16(const SharedUInt16& si) {
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->pruintDebug(
        MessageHandler::Level::Info,
        "SharedData::writeUInt16. UInt = %u", si->getVal()
    );
#endif

    uint16_t val = si.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    unsigned char* p = reinterpret_cast<unsigned char*>(&val);
    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(uint16_t));
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeUInt8(const SharedUInt8& si)
{
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->pruintDebug(
        "SharedData::writeUInt8. UInt = %u", si->getVal()
    );
#endif

    uint8_t val = si.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    unsigned char* p = reinterpret_cast<unsigned char*>(&val);
    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(uint8_t));
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeUChar(const SharedUChar& suc) {
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("SharedData::writeUChar");
#endif

    unsigned char val = suc.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    _currentStorage->push_back(val);
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeBool(const SharedBool& sb) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::writeBool");
#endif
    
    bool val = sb.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    _currentStorage->push_back(val ? 1 : 0);
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeString(const SharedString& ss) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::writeString");
#endif
    
    std::string tmpStr = ss.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    uint32_t length = static_cast<uint32_t>(tmpStr.size());
    unsigned char* p = reinterpret_cast<unsigned char*>(&length);
    
    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(uint32_t));
    _currentStorage->insert(_currentStorage->end(), tmpStr.data(), tmpStr.data() + length);
    
    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::writeWString(const SharedWString& ss) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::writeWString");
#endif

    std::wstring tmpStr = ss.getVal();
    MutexManager::instance()->dataSyncMutex.lock();
    uint32_t length = static_cast<uint32_t>(tmpStr.size());
    unsigned char* p = reinterpret_cast<unsigned char*>(&length);
    unsigned char* ws = reinterpret_cast<unsigned char*>(&tmpStr[0]);

    _currentStorage->insert(_currentStorage->end(), p, p + sizeof(uint32_t));
    _currentStorage->insert(_currentStorage->end(), ws, ws + length * sizeof(wchar_t));

    MutexManager::instance()->dataSyncMutex.unlock();
}

void SharedData::readFloat(SharedFloat& sf) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readFloat");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    
    float val = *reinterpret_cast<float*>(&_dataBlock[_pos]);
    _pos += sizeof(float);
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("Float = %f", val);
#endif

    sf.setVal(val);
}

void SharedData::readDouble(SharedDouble& sd) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readDouble");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    double val = *reinterpret_cast<double*>(&_dataBlock[_pos]);
    _pos += sizeof(double);
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("Double = %lf", val);
#endif

    sd.setVal(val);
}

void SharedData::readInt64(SharedInt64& si) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readInt64");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    int64_t val = *reinterpret_cast<int64_t*>(&_dataBlock[_pos]);
    _pos += sizeof(int64_t);
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("Int64 = %ld", val);
#endif

    si.setVal(val);
}

void SharedData::readInt32(SharedInt32& si) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readInt32");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    int32_t val = *reinterpret_cast<int32_t*>(&_dataBlock[_pos]);
    _pos += sizeof(int32_t);
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("Int32 = %d", val);
#endif

    si.setVal(val);
}

void SharedData::readInt16(SharedInt16& si) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readInt16");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    int16_t val = *reinterpret_cast<int16_t*>(&_dataBlock[_pos]);
    _pos += sizeof(int16_t);
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("Int16 = %d", val);
#endif

    si.setVal(val);
}

void SharedData::readInt8(SharedInt8& si) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readInt8");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    int8_t val = *reinterpret_cast<int8_t*>(&_dataBlock[_pos]);
    _pos += sizeof(int8_t);
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("Int8 = %d", val);
#endif

    si.setVal(val);
}

void SharedData::readUInt64(SharedUInt64& si) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readUInt64");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    uint64_t val = *reinterpret_cast<uint64_t*>(&_dataBlock[_pos]);
    _pos += sizeof(uint64_t);
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("UInt64 = %lu", val);
#endif

    si.setVal(val);
}

void SharedData::readUInt32(SharedUInt32& si) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readUInt32");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    uint32_t val = *reinterpret_cast<uint32_t*>(&_dataBlock[_pos]);
    _pos += sizeof(uint32_t);
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("UInt32 = %u", val);
#endif

    si.setVal(val);
}

void SharedData::readUInt16(SharedUInt16& si) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readUInt16");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    uint16_t val = *reinterpret_cast<uint16_t*>(&_dataBlock[_pos]);
    _pos += sizeof(uint16_t);
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("UInt16 = %u", val);
#endif

    si.setVal(val);
}

void SharedData::readUInt8(SharedUInt8& si) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readUInt8");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    uint8_t val = *reinterpret_cast<uint8_t*>(&_dataBlock[_pos]);
    _pos += sizeof(uint8_t);
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("UInt8 = %u", val);
#endif

    si.setVal(val);
}

void SharedData::readUChar(SharedUChar& suc) {
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("SharedData::readUChar");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    unsigned char c = _dataBlock[_pos];
    _pos += sizeof(unsigned char);
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("UChar = %d", c);
#endif
    suc.setVal(c);
}

void SharedData::readBool(SharedBool& sb) {
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("SharedData::readBool");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    bool b = _dataBlock[_pos] == 1;
    _pos += 1;
    MutexManager::instance()->dataSyncMutex.unlock();

#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug(MessageHandler::Level::Info, "Bool = %d", b);
#endif
    sb.setVal(b);
}

void SharedData::readString(SharedString& ss) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readString");
#endif
    MutexManager::instance()->dataSyncMutex.lock();
    
    uint32_t length = *reinterpret_cast<uint32_t*>(&_dataBlock[_pos]);
    _pos += sizeof(uint32_t);

    if (length == 0) {
        ss.setVal("");
        return;
    }

    std::string stringData(_dataBlock.begin() + _pos, _dataBlock.begin() + _pos + length);
    _pos += length;
    MutexManager::instance()->dataSyncMutex.unlock();
    
#ifdef __SGCT_NETWORK_DEBUG__ 
    MessageHandler::instance()->printDebug("String = '%s'", stringData);
#endif

    ss.setVal(std::move(stringData));
}

void SharedData::readWString(SharedWString& ss) {
#ifdef __SGCT_NETWORK_DEBUG__     
    MessageHandler::instance()->printDebug("SharedData::readWString");
#endif
    MutexManager::instance()->dataSyncMutex.lock();

    uint32_t length = *(reinterpret_cast<uint32_t*>(&_dataBlock[_pos]));
    _pos += sizeof(uint32_t);

    if (length == 0) {
        ss.setVal(std::wstring());
        return;
    }

    std::wstring stringData(_dataBlock.begin() + _pos, _dataBlock.begin() + _pos + length);
    _pos += length * sizeof(wchar_t);
    MutexManager::instance()->dataSyncMutex.unlock();

    ss.setVal(std::move(stringData));
}

} // namespace sgct