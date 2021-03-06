//#include <GameEngine.GraphicsEcosystem.Precompiled.h>
#include <RenderTargets.Vulkan.h>

#include <CommandQueue.Vulkan.h>
#include <Swapchain.Vulkan.h>
#include <RenderPass.Vulkan.h>
#include <Framebuffer.Vulkan.h>

/// -------------------------------------------------------------------------------------------------------------------
/// RenderTargets
/// -------------------------------------------------------------------------------------------------------------------

apemodevk::RenderTargets::RenderTargets()
{
    WriteFrame = 0;
    ReadFrame  = kFrameMaxCount - 1;
    FrameCount = kFrameMaxCount;

    AttachmentCount  = 0;
    DepthStencilFormat = VK_FORMAT_UNDEFINED;
    for (auto & ColorFormat : pColorFormats)
        ColorFormat = VK_FORMAT_UNDEFINED;
}

/// -------------------------------------------------------------------------------------------------------------------

apemodevk::RenderTargets::~RenderTargets()
{
}

/// -------------------------------------------------------------------------------------------------------------------

bool apemodevk::RenderTargets::RecreateResourcesFor (GraphicsDevice & InGraphicsNode,
                                                VkSwapchainKHR   InSwapchainHandle,
                                                VkFormat         InColorFmt,
                                                uint32_t         InColorWidth,
                                                uint32_t         InColorHeight,
                                                VkFormat         InDepthStencilFmt,
                                                uint32_t         InDepthStencilWidth,
                                                uint32_t         InDepthStencilHeight,
                                                bool             InDepthStencilReversed)
{
    apemode_assert (InSwapchainHandle != nullptr, "Swapchain is not initialized.");

    uint32_t OutSwapchainBufferCount = 0;
    if (ResultHandle::Succeeded (vkGetSwapchainImagesKHR (
            InGraphicsNode, InSwapchainHandle, &OutSwapchainBufferCount, nullptr)))
    {
        apemode_assert (OutSwapchainBufferCount != 1,
                             "Frame count must be at least 2,"
                             "otherwise this class is redundand");
        apemode_assert (OutSwapchainBufferCount <= kFrameMaxCount,
                             "Frame count overlow (%u vs %u)",
                             OutSwapchainBufferCount,
                             kFrameMaxCount);

        std::vector<VkImage> SwapchainBufferImgs (OutSwapchainBufferCount);
        if (apemode_unlikely (
                ResultHandle::Failed (vkGetSwapchainImagesKHR (InGraphicsNode,
                                                               InSwapchainHandle,
                                                               &OutSwapchainBufferCount,
                                                               SwapchainBufferImgs.data ()))))
        {
            apemode_halt("vkGetSwapchainImagesKHR failed.");
            return false;
        }

        WriteFrame = 0;
        ReadFrame  = OutSwapchainBufferCount - 1;

        AttachmentCount = 1;
        FrameCount        = OutSwapchainBufferCount;

        for (uint32_t FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
        {
            for (uint32_t AttIdx = 0; AttIdx < AttachmentCount; AttIdx++)
            {
                ppColorViews[ FrameIdx ][ AttIdx ] = ColorResourceView::MakeNewLinked ();
                pColorFormats[ AttIdx ]            = InColorFmt;

                if (apemode_unlikely (
                        !ppColorViews[ FrameIdx ][ AttIdx ]->RecreateResourcesFor (
                            InGraphicsNode, InColorFmt, SwapchainBufferImgs[ FrameIdx ])))
                {
                    apemode_error("Failed to create RTV.");
                    return false;
                }

                // TODO Refactor
                ppColorViews[ FrameIdx ][ AttIdx ]->Width  = InColorWidth;
                ppColorViews[ FrameIdx ][ AttIdx ]->Height = InColorHeight;
            }
        }

        RenderArea.offset.x      = 0;
        RenderArea.offset.y      = 0;
        RenderArea.extent.width  = InColorWidth;
        RenderArea.extent.height = InColorHeight;

        for (uint32_t AttIdx = 0; AttIdx < AttachmentCount; AttIdx++)
        {
            pScissors[ AttIdx ].offset.x      = 0;
            pScissors[ AttIdx ].offset.y      = 0;
            pScissors[ AttIdx ].extent.width  = InColorWidth;
            pScissors[ AttIdx ].extent.height = InColorHeight;

            pViewports[ AttIdx ].x        = 0.0f;
            pViewports[ AttIdx ].y        = 0.0f;
            pViewports[ AttIdx ].width    = static_cast<float> (InColorWidth);
            pViewports[ AttIdx ].height   = static_cast<float> (InColorHeight);
            pViewports[ AttIdx ].minDepth = 0.0f;
            pViewports[ AttIdx ].maxDepth = 1.0f;
        }

        if (apemode_likely (InDepthStencilFmt != VK_FORMAT_UNDEFINED))
        {
            apemode_assert (InDepthStencilWidth != 0 && InDepthStencilHeight != 0,
                                 "DSV dimensions.");

            DepthStencilFormat = InDepthStencilFmt;
            for (uint32_t FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
            {
                pDepthStencilViews[ FrameIdx ] = DepthStencilResourceView::MakeNewLinked ();

                if (apemode_unlikely (!pDepthStencilViews[ FrameIdx ]->RecreateResourcesFor (
                        InGraphicsNode,
                        InDepthStencilFmt,
                        InDepthStencilWidth,
                        InDepthStencilHeight,
                        InDepthStencilReversed ? 0.0f : 1.0f,
                        0,
                        VK_IMAGE_LAYOUT_UNDEFINED)))
                {
                    apemode_error ("Failed to create DSV.");
                    return false;
                }
            }
        }
    }

    if (apemode_likely (AttachmentCount))
    {
        static const uint32_t kDefaultSubpassId = 0;

        apemodevk::RenderPassBuilder RenderPassBuilder;
        RenderPassBuilder.Reset (2, 0, 1);
        auto ColorId = RenderPassBuilder.AddAttachment (InColorFmt,
                                                        VK_SAMPLE_COUNT_1_BIT,
                                                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                                        VK_ATTACHMENT_LOAD_OP_LOAD,
                                                        VK_ATTACHMENT_STORE_OP_STORE,
                                                        false,
                                                        true);
        auto DepthId
            = RenderPassBuilder.AddAttachment (InDepthStencilFmt,
                                               VK_SAMPLE_COUNT_1_BIT,
                                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                               VK_ATTACHMENT_LOAD_OP_LOAD,
                                               VK_ATTACHMENT_STORE_OP_STORE,
                                               VK_ATTACHMENT_LOAD_OP_LOAD,
                                               VK_ATTACHMENT_STORE_OP_STORE,
                                               false);

        RenderPassBuilder.ResetSubpass (0, 1, 0, 0);
        RenderPassBuilder.AddColorToSubpass (0, ColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        RenderPassBuilder.SetDepthToSubpass (0, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        pRenderPass = RenderPassBuilder.RecreateRenderPass (InGraphicsNode);

        apemodevk::FramebufferBuilder FramebufferBuilder;
        for (uint32_t FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
        {
            for (uint32_t AttIdx = 0; AttIdx < AttachmentCount; AttIdx++)
            {
                FramebufferBuilder.Attach (*ppColorViews[ FrameIdx ][ AttIdx ]);
            }

            if (apemode_likely (pDepthStencilViews[ FrameIdx ]))
            {
                FramebufferBuilder.Attach (*pDepthStencilViews[ FrameIdx ]);
            }

            FramebufferBuilder.SetRenderPass (*pRenderPass);
            ppFramebuffers[ FrameIdx ] = FramebufferBuilder.RecreateFramebuffer (InGraphicsNode);
            apemode_assert (ppFramebuffers[ FrameIdx ] != nullptr,
                                 "Failed to create framebuffer for frame %u",
                                 FrameIdx);

            FramebufferBuilder.Reset ();
        }
    }

    return true;
}

/// -------------------------------------------------------------------------------------------------------------------

void apemodevk::RenderTargets::SetRenderTargetClearValues (uint32_t                  InOffset,
                                                      uint32_t                  InClearValueCount,
                                                      VkClearColorValue const * InClearValues)
{
    const bool bIsInputLikelyOk = InClearValues != nullptr && InClearValueCount != 0;
    const bool bIsRangeOk = (InOffset < (AttachmentCount - 1))
                            && (InClearValueCount <= (AttachmentCount - InOffset));

    apemode_assert(bIsInputLikelyOk && bIsInputLikelyOk, "Invalid input.");

    for (uint32_t CurrentFrame = 0; CurrentFrame < FrameCount; CurrentFrame++)
    {
        for (uint32_t i = InOffset, j = 0; j < InClearValueCount; ++i, ++j)
        {
            //TODO Simd instruction
            // VkClearColorValue is union, that is why member-size assigning with uint32`s...
            ClearValues[CurrentFrame][i].color.uint32[0] = InClearValues[j].uint32[0];
            ClearValues[CurrentFrame][i].color.uint32[1] = InClearValues[j].uint32[1];
            ClearValues[CurrentFrame][i].color.uint32[2] = InClearValues[j].uint32[2];
            ClearValues[CurrentFrame][i].color.uint32[3] = InClearValues[j].uint32[3];
        }
    }
}

/// -------------------------------------------------------------------------------------------------------------------

void apemodevk::RenderTargets::SetDepthStencilClearValue (
    VkClearDepthStencilValue DepthStencilClearValue)
{
    for (uint32_t CurrentFrame = 0; CurrentFrame < FrameCount; CurrentFrame++)
    {
        ClearValues[CurrentFrame][AttachmentCount].depthStencil = DepthStencilClearValue;
    }
}

/// -------------------------------------------------------------------------------------------------------------------

void apemodevk::RenderTargets::AdvanceFrameCounters ()
{
    ReadFrame  = WriteFrame;
    WriteFrame = (WriteFrame + 1) % FrameCount;
}

uint32_t apemodevk::RenderTargets::GetWriteFrame () const
{
    return WriteFrame;
}

uint32_t apemodevk::RenderTargets::GetReadFrame () const
{
    return ReadFrame;
}

uint32_t apemodevk::RenderTargets::GetAttachmentCount () const
{
    return AttachmentCount;
}

apemodevk::RenderPass const * apemodevk::RenderTargets::GetRenderPass () const
{
    return pRenderPass;
}

apemodevk::Framebuffer const * apemodevk::RenderTargets::GetWriteFramebuffer() const
{
    return ppFramebuffers[WriteFrame];
}

/// -------------------------------------------------------------------------------------------------------------------
/// RenderTargets BeginEndScope
/// -------------------------------------------------------------------------------------------------------------------

apemodevk::RenderTargets::BeginEndScope::BeginEndScope (CommandBuffer &   CmdBuffer,
                                                   RenderTargets & RenderTargets)
    : AssociatedCmdList (CmdBuffer)
    , AssociatedRenderTargets (RenderTargets)
{
    apemode_assert (CmdBuffer.bIsInBeginEndScope,
                         "Command recording was not started.");
    apemode_assert (!!RenderTargets.pRenderPass
                             && !!RenderTargets.ppFramebuffers[ RenderTargets.WriteFrame ],
                         "Render targets was not successfully initialized.");

    TInfoStruct<VkRenderPassBeginInfo> RenderPassBeginDesc;
    RenderPassBeginDesc->renderArea      = RenderTargets.RenderArea;
    RenderPassBeginDesc->pClearValues    = RenderTargets.ClearValues[ RenderTargets.WriteFrame ];
    RenderPassBeginDesc->clearValueCount = RenderTargets.AttachmentCount + 1;
    RenderPassBeginDesc->framebuffer     = *RenderTargets.ppFramebuffers[ RenderTargets.WriteFrame ];
    RenderPassBeginDesc->renderPass      = *RenderTargets.pRenderPass;

    //TODO Is it correct? I`m not quite sure, see docs.
    //TODO CmdBuffer.Type == CommandBuffer::kCommandListType_Direct should be moved to a function
    //TODO Branching can be removed with static_cast and additional assertion (for CmdBuffer.Type {0, 1} range).
    auto SubpassContents = CmdBuffer.eType == CommandBuffer::kCommandListType_Direct
        ? VK_SUBPASS_CONTENTS_INLINE
        : VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS;

    vkCmdBeginRenderPass (CmdBuffer, RenderPassBeginDesc, SubpassContents);
    vkCmdSetViewport (CmdBuffer, 0, RenderTargets.AttachmentCount, RenderTargets.pViewports);
    vkCmdSetScissor (CmdBuffer, 0, RenderTargets.AttachmentCount, RenderTargets.pScissors);

    CmdBuffer.pRenderPass  = RenderTargets.pRenderPass;
    CmdBuffer.pFramebuffer = RenderTargets.ppFramebuffers[RenderTargets.WriteFrame];
}

/// -------------------------------------------------------------------------------------------------------------------

apemodevk::RenderTargets::BeginEndScope::~BeginEndScope ()
{
    AssociatedCmdList.pRenderPass = nullptr;
    AssociatedCmdList.pFramebuffer = nullptr;

    vkCmdEndRenderPass(AssociatedCmdList);
}

/// -------------------------------------------------------------------------------------------------------------------
