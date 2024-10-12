/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <cassert>

#include "util/util_log.h"
#include "util/util_string.h"

#include "vkd3d-proton/vkd3d-proton_interfaces.h"
#include "../inc/nvofapi/nvOpticalFlowD3D12.h"

#include "nvofapi.h"

namespace nvofapi {

    NvOFInstanceD3D12::NvOFInstanceD3D12(ID3D12Device* pD3D12Device) {
        // Query for the extension interface
        // Grab the vk triad
        HRESULT status = pD3D12Device->QueryInterface(IID_PPV_ARGS(&m_device));

        if (S_OK != status) {
            throw std::invalid_argument("Failed to query interface for m_device");
        }

        m_device->GetVulkanHandles(&m_vkInstance,
            &m_vkPhysicalDevice,
            &m_vkDevice);

        status = pD3D12Device->QueryInterface(IID_PPV_ARGS(&m_d3ddevice));

        if (S_OK != status) {
            throw std::invalid_argument("Failed to query interface for m_d3ddevice");
        }

        status = pD3D12Device->QueryInterface(IID_PPV_ARGS(&m_deviceExt));

        if (S_OK != status) {
            throw std::invalid_argument("Failed to query interface for m_deviceExt");
        }
    }

    bool NvOFInstanceD3D12::Initialize() {
        m_library = LoadLibraryA("winevulkan.dll");
        if (!m_library) {
            return false;
        }

        // Confirm that OPTICAL_FLOW extension is available
        if (!m_deviceExt->GetExtensionSupport(D3D12_VK_NV_OPTICAL_FLOW)) {
            dxvk::log::info("Missing VK_NV_optical_flow extension!");
            return false;
        }

        m_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(m_library, "vkGetInstanceProcAddr");
        m_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)m_vkGetInstanceProcAddr(m_vkInstance, "vkGetDeviceProcAddr");
        m_vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)m_vkGetInstanceProcAddr(m_vkInstance, "vkGetPhysicalDeviceQueueFamilyProperties");

        m_vkCreateImageView = (PFN_vkCreateImageView)m_vkGetDeviceProcAddr(m_vkDevice, "vkCreateImageView");
        m_vkDestroyImageView = (PFN_vkDestroyImageView)m_vkGetDeviceProcAddr(m_vkDevice, "vkDestroyImageView");

        // Populate the optical flow related info here
        // fail to create if optical flow extension is unsupported
        m_vkCreateOpticalFlowSessionNV = (PFN_vkCreateOpticalFlowSessionNV)m_vkGetDeviceProcAddr(m_vkDevice, "vkCreateOpticalFlowSessionNV");
        m_vkDestroyOpticalFlowSessionNV = (PFN_vkDestroyOpticalFlowSessionNV)m_vkGetDeviceProcAddr(m_vkDevice, "vkDestroyOpticalFlowSessionNV");
        m_vkBindOpticalFlowSessionImageNV = (PFN_vkBindOpticalFlowSessionImageNV)m_vkGetDeviceProcAddr(m_vkDevice, "vkBindOpticalFlowSessionImageNV");
        m_vkCmdOpticalFlowExecuteNV = (PFN_vkCmdOpticalFlowExecuteNV)m_vkGetDeviceProcAddr(m_vkDevice, "vkCmdOpticalFlowExecuteNV");

        // Get the OFA queue
        m_vkQueueFamilyIndex = GetVkOFAQueue();

        D3D12_COMMAND_QUEUE_DESC desc{};
        if (m_device->CreateInteropCommandQueue(&desc, m_vkQueueFamilyIndex, &m_commandQueue)
            != S_OK) {
            return false;
        }
        if (m_device->CreateInteropCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, m_vkQueueFamilyIndex, &m_cmdAllocator)
            != S_OK) {
            return false;
        }
        for (int i = 0; i < CMDS_IN_FLIGHT; i++) {
            if (m_d3ddevice->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, (D3D12_COMMAND_LIST_FLAGS)0, IID_PPV_ARGS(&m_cmdList[i]))
                != S_OK) {
                return false;
            }
        }
        return true;
    }

    NV_OF_STATUS NvOFInstanceD3D12::RegisterBuffer(const NV_OF_REGISTER_RESOURCE_PARAMS_D3D12* registerParams) {
        NV_OF_REGISTER_RESOURCE_PARAMS_VK vkParams{};
        dxvk::log::info(
            dxvk::str::format("RegisterBuffer DX: resource: ",
                registerParams->resource, " inputFencePoint: ",
                registerParams->inputFencePoint.fence, " ouputFencePoint: ",
                registerParams->outputFencePoint.fence));
        // Convert D3D12 params to VK params
        //
        // ID3D12Resource -> VK Image / VkFormat pair
        vkParams.hOFGpuBuffer = registerParams->hOFGpuBuffer;
        uint64_t offset;
        m_device->GetVulkanResourceInfo1(registerParams->resource, (UINT64*)&vkParams.image, &offset, &vkParams.format);

        // ID3D12 fence to timeline semaphore
        // no inputFencePoint/outputFencePoint equivalents for VK, leaving as
        // no-op :(
        assert(registerParams->inputFencePoint.fence == nullptr);
        assert(registerParams->inputFencePoint.value == 0);
        assert(registerParams->outputFencePoint.fence == nullptr);
        assert(registerParams->outputFencePoint.value == 0);
        return ((NvOFInstance*)this)->RegisterBuffer(&vkParams);
    }

    NV_OF_STATUS NvOFInstanceD3D12::Execute(const NV_OF_EXECUTE_INPUT_PARAMS_D3D12* inParams, NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12* outParams) {
        // Convert the D3D12 parameters to VK parameters
        NV_OF_EXECUTE_INPUT_PARAMS_VK vkInputParams{};
        NV_OF_EXECUTE_OUTPUT_PARAMS_VK vkOutputParams{};

        vkInputParams.inputFrame = inParams->inputFrame;
        vkInputParams.referenceFrame = inParams->referenceFrame;
        vkInputParams.externalHints = inParams->externalHints;
        vkInputParams.disableTemporalHints = inParams->disableTemporalHints;
        vkInputParams.hPrivData = inParams->hPrivData;
        vkInputParams.numRois = inParams->numRois;
        vkInputParams.roiData = inParams->roiData;

        vkOutputParams.outputBuffer = outParams->outputBuffer;
        vkOutputParams.outputCostBuffer = outParams->outputCostBuffer;
        vkOutputParams.hPrivData = outParams->hPrivData;
        vkOutputParams.bwdOutputBuffer = outParams->bwdOutputBuffer;
        vkOutputParams.bwdOutputCostBuffer = outParams->bwdOutputCostBuffer;
        vkOutputParams.globalFlowBuffer = outParams->globalFlowBuffer;

        // Use vkd3d-proton's interop functionality to grab a VkCommandBuffer
        // that we record our commands into. Work submission and syncrhonization
        // happens using D3D12.
        m_cmdList[m_cmdListIndex]->Reset(m_cmdAllocator, nullptr);

        for (int i = 0; i < inParams->numFencePoints; i++) {
            m_commandQueue->Wait(inParams->fencePoint[i].fence, inParams->fencePoint[i].value);
        }

        VkCommandBuffer vkCmdBuf;
        m_device->BeginVkCommandBufferInterop(m_cmdList[m_cmdListIndex], &vkCmdBuf);

        this->RecordCmdBuf(&vkInputParams, &vkOutputParams, vkCmdBuf);

        m_device->EndVkCommandBufferInterop(m_cmdList[m_cmdListIndex]);
        m_cmdList[m_cmdListIndex]->Close();

        m_commandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&m_cmdList[m_cmdListIndex]);

        m_commandQueue->Signal(outParams->fencePoint->fence, outParams->fencePoint->value);

        m_cmdListIndex++;
        if (m_cmdListIndex >= CMDS_IN_FLIGHT)
            m_cmdListIndex = 0;

        return NV_OF_SUCCESS;
    }

}
