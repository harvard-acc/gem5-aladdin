#include "systolic_array.h"

void SystolicArray::issueDmaRead() {
  DPRINTF(SystolicArray, "Start DMA reads.\n");
  // Read inputs.
  int inputSize = inputRows * inputCols * channels * sizeof(float);
  uint8_t* inputData = new uint8_t[inputSize]();
  DmaEvent* inputDmaEvent = new DmaEvent(this, inputBaseAddr);
  splitAndSendDmaRequest(
      inputBaseAddr, inputSize, true, inputData, inputDmaEvent);
  // Read weights.
  int weightSize =
      weightRows * weightCols * channels * numOfmaps * sizeof(float);
  uint8_t* weightData = new uint8_t[weightSize]();
  DmaEvent* weightDmaEvent = new DmaEvent(this, weightBaseAddr);
  splitAndSendDmaRequest(
      weightBaseAddr, weightSize, true, weightData, weightDmaEvent);
}

void SystolicArray::issueDmaWrite() {
  DPRINTF(SystolicArray, "Start DMA writes.\n");
  int outputSize = outputRows * outputCols * numOfmaps * sizeof(float);
  uint8_t* outputData = new uint8_t[outputSize]();
  DmaEvent* outputDmaEvent = new DmaEvent(this, outputBaseAddr);
  splitAndSendDmaRequest(
      outputBaseAddr, outputSize, false, outputData, outputDmaEvent);
}

int SystolicArray::genSramReads() {
  int localCycles = 0;
  int weightSize = weightRows * weightCols * channels;
  int ofmapSize = outputRows * outputCols;

  std::vector<int> rowBaseAddr(peArrayRows, 0);
  std::vector<int> colBaseAddr(peArrayCols, 0);
  // When the leftmost column of PEs read the input windows, every next row will
  // start the reading by a delay of 1 cycle relative to the previous one, as if
  // the clock of the next row was delayed by 1 cycle. rowClkOffset keeps track
  // of the clock offsets of all the rows. A negative offset value means the
  // corresponding row is still waiting to start the reading. Similarly, the
  // top column read the weights in this delayed fashion.
  std::vector<int> rowClkOffset(peArrayRows, 0);
  std::vector<int> colClkOffset(peArrayCols, 0);
  // The pixel index of the output feature map that every row is responsible
  // for.
  std::vector<int> rowOfmapIdx(peArrayRows, 0);
  // The weight fold every row is working with.
  std::vector<int> rowWeightFold(peArrayRows, 0);
  // This barrier ensures that when we reach the last output fold, all rows are
  // synchronized for the next weight fold.
  std::vector<bool> rowFoldBarrier(peArrayRows, false);
  // The weight fold every column is working with.
  std::vector<int> colWeightFold(peArrayCols, 0);
  // The output fold every column is working with.
  std::vector<int> colOutputFold(peArrayCols, 0);
  std::vector<bool> columnDone(peArrayCols, false);

  // Initialize tracking variables
  for (int r = 0; r < peArrayRows; r++) {
    int baseRowId = floor(r / outputCols) * stride;
    int baseColId = r % outputCols * stride;
    rowBaseAddr[r] = baseRowId * inputCols * channels + baseColId * channels;
    rowClkOffset[r] = r < ofmapSize ? -r : INT_MIN;
    rowOfmapIdx[r] = r;
  }
  for (int c = 0; c < peArrayCols; c++) {
    colBaseAddr[c] = c * weightSize;
    if (c < numOfmaps) {
      colClkOffset[c] = -c;
    } else {
      colClkOffset[c] = INT_MIN;
      columnDone[c] = true;
    }
  }

  bool ifmapDone = false;
  bool weightDone = false;
  // Work if either ifmaps or filters are remaining to be processed
  while (ifmapDone == false || weightDone == false) {
    for (int r = 0; r < peArrayRows; r++) {
      // Generate SRAM trace for inputs.
      if (rowClkOffset[r] >= 0) {
        // If this row is valid for reading inputs, generate the SRAM access.
        int addrRowOffset = floor(rowClkOffset[r] / (weightCols * channels)) *
                            inputCols * channels;
        int addrColOffset = rowClkOffset[r] % (weightCols * channels);
        Addr inputAddr =
            rowBaseAddr[r] + addrRowOffset + addrColOffset + inputBaseAddr;
        DPRINTF(SystolicArrayVerbose,
                "Clock cycle: %d, read inputs, addr: %#x\n", localCycles,
                inputAddr);
      } else {
        // This row is still waiting to start reading inputs.
      }

      // Increment the clock for this row and check if it has finished an ofmap
      // pixel, that is, it has read all the pixels in a input window.
      rowClkOffset[r] = rowClkOffset[r] + 1;
      if (rowClkOffset[r] > 0 && rowClkOffset[r] % weightSize == 0) {
        // This row has finished the output pixel and is ready to work for the
        // next.
        rowOfmapIdx[r] += peArrayRows;
        if (rowOfmapIdx[r] < ofmapSize) {
          // There are still remaining output pixels for this row to work with,
          // so adjust the row clock offset and row base address.
          rowClkOffset[r] = 0;

          int baseRowId = floor(rowOfmapIdx[r] / outputCols) * stride;
          int baseColId = rowOfmapIdx[r] % outputCols * stride;
          rowBaseAddr[r] =
              baseRowId * inputCols * channels + baseColId * channels;
        } else {
          // This row has finished its share of the ofmaps, so it's ready
          // for the next set of weights (weight fold), if there is any. But
          // because some rows may haven't finished their output pixels, so this
          // row will wait until all the others have finished their work before
          // streaming in a new weight fold.
          rowWeightFold[r] += 1;
          if (rowWeightFold[r] < numWeightFolds) {
            rowOfmapIdx[r] = r;

            int baseRowId = floor(r / outputCols) * stride;
            int baseColId = r % outputCols * stride;
            rowBaseAddr[r] =
                baseRowId * inputCols * channels + baseColId * channels;

            // Stall this row from proceeding until all the rows reach the
            // weight fold boundary.
            if (r != 0 && (rowWeightFold[r] > rowWeightFold[r - 1] ||
                           rowFoldBarrier[r - 1] == true)) {
              rowClkOffset[r] = INT_MIN;
              rowFoldBarrier[r] = true;
            } else {
              rowClkOffset[r] = 0;
            }
          } else {
            rowClkOffset[r] = INT_MIN;
          }
        }
      }
    }

    // The barrier insertion and recovery is in separate loops to ensure that
    // in a given clock cycle insertion for all rows strictly happen before
    // the release. The flag ensures only one row is released per cycle.
    bool flag = false;
    for (int r = 0; r < peArrayRows; r++) {
      if (rowFoldBarrier[r] == true && flag == false) {
        // Release the row if the previous row has been released.
        if (rowWeightFold[r] == rowWeightFold[r - 1] &&
            rowFoldBarrier[r - 1] == false) {
          rowFoldBarrier[r] = false;
          flag = true;
          rowClkOffset[r] = rowClkOffset[r - 1] - 1;
        }
      }
    }
    ifmapDone = true;
    for (int r = 0; r < peArrayRows; r++) {
      if (rowClkOffset[r] > 0)
        ifmapDone = false;
    }

    for (int c = 0; c < peArrayCols; c++) {
      // Generate SRAM reading trace for filters
      if (colClkOffset[c] >= 0) {
        // This column is valid for reading weight pixels.
        Addr weightAddr = colBaseAddr[c] + colClkOffset[c] + weightBaseAddr;
        DPRINTF(SystolicArrayVerbose,
                "Clock cycle: %d, read weights, addr: %#x\n", localCycles,
                weightAddr);
      } else {
        // Not allowed to read at the moment.
      }

      colClkOffset[c] += 1;
      if (colClkOffset[c] > 0 && colClkOffset[c] % weightSize == 0) {
        // This column has finished a weight. Before we move on to the next
        // weight fold, we need to make sure that we have finished all the
        // output folds for this weight.
        colOutputFold[c] += 1;
        if (colOutputFold[c] < numOutputFolds) {
          // There are remaining output folds.
          colClkOffset[c] = 0;
        } else {
          // Proceed to the next weight fold.
          colWeightFold[c] += 1;
          // Some of the columns may not be active in the last fold. This filter
          // ID check ensures only valid columns are active.
          int filt_id = colWeightFold[c] * peArrayCols + c;
          if (colWeightFold[c] < numWeightFolds && filt_id < numOfmaps) {
            colClkOffset[c] = 0;
            colOutputFold[c] = 0;
            int base = filt_id * weightSize;
            colBaseAddr[c] = base;
          } else {
            // This column has finished its work.
            colClkOffset[c] = INT_MIN;
            columnDone[c] = true;
          }
        }
      }
    }
    weightDone = true;
    for (int c = 0; c < peArrayCols; c++) {
      if (columnDone[c] == false)
        weightDone = false;
    }
    localCycles++;
  }
  return localCycles;
}

int SystolicArray::genSramWrites() {
  int localCycles = 0;
  int weightSize = weightRows * weightCols * channels;
  int ofmapSize = outputRows * outputCols;
  int activeRows = std::min(peArrayRows, ofmapSize);
  int activeCols = std::min(peArrayCols, numOfmaps);
  int finishedOutputFolds = 0;
  int finishedWeightFolds = 0;

  // Fast forward to the cycle when all the output pixels of the first column
  // PEs have been generated.
  localCycles = weightSize + activeCols - 1;
  while (finishedOutputFolds < numOutputFolds ||
         finishedWeightFolds < numWeightFolds) {
    activeRows = std::min(peArrayRows, numOutputFolds - finishedOutputFolds);
    for (int r = 0; r < activeRows; r++) {
      for (int c = 0; c < activeCols; c++) {
        int outputAddr = outputBaseAddr +
                         (finishedOutputFolds * peArrayRows + r) * numOfmaps +
                         (finishedWeightFolds * peArrayCols + c);
        DPRINTF(SystolicArrayVerbose,
                "Clock cycle: %d, write outputs, addr: %d\n",
                localCycles + r - c, outputAddr);
      }
    }
    finishedOutputFolds++;
    if (finishedOutputFolds == numOutputFolds) {
      // All output pixels are generated for the weight fold.
      finishedWeightFolds++;
      if (finishedWeightFolds < numWeightFolds) {
        // There are remaining weights.
        finishedOutputFolds = 0;
        activeCols =
            std::min(peArrayCols, numWeightFolds - finishedWeightFolds);
        localCycles += std::max(weightSize, activeRows);
      } else {
        localCycles += activeRows;
      }
    } else {
      // More output fold to process. Go to the next cycle when all the output
      // pixels of the first columns PEs have been generated.
      localCycles += std::max(weightSize, activeRows);
    }
  }
  return localCycles;
}

SystolicArray* SystolicArrayParams::create() { return new SystolicArray(this); }

