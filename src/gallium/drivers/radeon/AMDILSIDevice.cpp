//===-- AMDILSIDevice.cpp - TODO: Add brief description -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//==-----------------------------------------------------------------------===//
#include "AMDILSIDevice.h"
#include "AMDILEvergreenDevice.h"
#include "AMDILNIDevice.h"
#include "AMDILSubtarget.h"

using namespace llvm;

AMDILSIDevice::AMDILSIDevice(AMDILSubtarget *ST)
  : AMDILEvergreenDevice(ST)
{
}
AMDILSIDevice::~AMDILSIDevice()
{
}

size_t
AMDILSIDevice::getMaxLDSSize() const
{
  if (usesHardware(AMDILDeviceInfo::LocalMem)) {
    return MAX_LDS_SIZE_900;
  } else {
    return 0;
  }
}

uint32_t
AMDILSIDevice::getGeneration() const
{
  return AMDILDeviceInfo::HD7XXX;
}

std::string
AMDILSIDevice::getDataLayout() const
{
    return std::string("e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16"
      "-i32:32:32-i64:64:64-f32:32:32-f64:64:64-f80:32:32"
      "-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64"
      "-v96:128:128-v128:128:128-v192:256:256-v256:256:256"
      "-v512:512:512-v1024:1024:1024-v2048:2048:2048"
      "-n8:16:32:64");
}