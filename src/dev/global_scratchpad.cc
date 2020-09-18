#include "dev/global_scratchpad.hh"
#include "debug/GlobalScratchpad.hh"

GlobalScratchpad::GlobalScratchpad(const Params* p)
    : PioDevice(p), pioAddr(p->pio_addr), pioSize(p->pio_size),
      chunk(p->pio_size) {}

AddrRangeList GlobalScratchpad::getAddrRanges() const {
    DPRINTF(GlobalScratchpad,
            "Global scratchpad registering addr range at %#x size %#x\n",
            pioAddr, pioSize);
    AddrRangeList ranges;
    ranges.push_back(RangeSize(pioAddr, pioSize));
    return ranges;
}

Tick GlobalScratchpad::read(PacketPtr pkt) {
    assert(pkt->getAddr() >= pioAddr);
    assert(pkt->getAddr() < pioAddr + pioSize);

    int offset = pkt->getAddr() - pioAddr;
    DPRINTF(
        GlobalScratchpad, "Read data at %#x size=%d\n", offset, pkt->getSize());
    chunk.readData(offset, pkt->getPtr<uint8_t>(), pkt->getSize());

    pkt->makeAtomicResponse();
    return 1;
}

Tick GlobalScratchpad::write(PacketPtr pkt) {
    assert(pkt->getAddr() >= pioAddr);
    assert(pkt->getAddr() < pioAddr + pioSize);

    int offset = pkt->getAddr() - pioAddr;
    DPRINTF(GlobalScratchpad, "Write data at %#x size=%d\n", offset,
            pkt->getSize());
    chunk.writeData(offset, pkt->getPtr<uint8_t>(), pkt->getSize());

    pkt->makeAtomicResponse();
    return 1;
}

GlobalScratchpad* GlobalScratchpadParams::create() {
    return new GlobalScratchpad(this);
}
