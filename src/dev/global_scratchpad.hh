#ifndef __DEV_GLOBAL_SCRATCHPAD_HH__
#define __DEV_GLOBAL_SCRATCHPAD_HH__

#include "dev/dma_device.hh"
#include "params/GlobalScratchpad.hh"

class DataChunk {
   public:
    DataChunk(int size) : chunk(size) {}

    void writeData(int index, uint8_t* data, int size) {
        assert(index < chunk.size());
        memcpy(&chunk[index], data, size);
    }

    void readData(int index, uint8_t* data, int size) {
        assert(index < chunk.size());
        memcpy(data, &chunk[index], size);
    }

   protected:
    std::vector<uint8_t> chunk;
};

class GlobalScratchpad : public PioDevice {
   public:
    typedef GlobalScratchpadParams Params;
    GlobalScratchpad(const Params* p);
    virtual ~GlobalScratchpad() {}

   protected:
    virtual AddrRangeList getAddrRanges() const override;
    virtual Tick read(PacketPtr pkt) override;
    virtual Tick write(PacketPtr pkt) override;

    // Base and length of PIO register space
    Addr pioAddr;
    Addr pioSize;

    // The actual data store for the scratchpad.
    DataChunk chunk;
};

#endif  // __DEV_GLOBAL_SCRATCHPAD_HH__
