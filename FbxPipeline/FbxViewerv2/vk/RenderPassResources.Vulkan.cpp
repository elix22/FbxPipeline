//#include <GameEngine.GraphicsEcosystem.Precompiled.h>
#include <Swapchain.Vulkan.h>
#include <RenderPass.Vulkan.h>
#include <Framebuffer.Vulkan.h>
#include <CommandQueue.Vulkan.h>

#include <RenderPassResources.Vulkan.h>

/// -------------------------------------------------------------------------------------------------------------------

union ExposedAttachmentHash {
    struct
    {
        uint32_t AttachmentId;
        uint32_t FrameIdx;
    };

    uint64_t AttachmentHash;
};

/// -------------------------------------------------------------------------------------------------------------------
/// RenderPassResources
/// -------------------------------------------------------------------------------------------------------------------

static bool ExtractSwapchainBuffers (apemode::GraphicsDevice &          InGraphicsNode,
                                     apemode::Swapchain &               InSwapchain,
                                     uint32_t                        InFrameCount,
                                     std::vector<VkImage> & OutSwapchainBufferImgs)
{
    _Game_engine_Assert (InGraphicsNode.IsValid () && InSwapchain.hSwapchain.IsNotNull (),
                         "Not initialized.");

    uint32_t OutSwapchainBufferCount = 0;
    if (apemode_likely (apemode::ResultHandle::Succeeded (vkGetSwapchainImagesKHR (
            InGraphicsNode, InSwapchain.hSwapchain, &OutSwapchainBufferCount, nullptr))))
    {
        _Game_engine_Assert (OutSwapchainBufferCount != InFrameCount,
                             "Frame count does not match buffer img count (%u, %u)",
                             OutSwapchainBufferCount,
                             InFrameCount);

        OutSwapchainBufferImgs.resize (OutSwapchainBufferCount, VkImage (nullptr));
        if (apemode_likely (apemode::ResultHandle::Succeeded (
                vkGetSwapchainImagesKHR (InGraphicsNode,
                                         InSwapchain.hSwapchain,
                                         &OutSwapchainBufferCount,
                                         OutSwapchainBufferImgs.data ()))))
        {
            return true;
        }

        OutSwapchainBufferImgs.resize(0);
    }

    _Game_engine_Halt ("vkGetSwapchainImagesKHR failed.");
    return false;
}

void apemode::RenderPassResources::SetWriteFrame (uint32_t InWriteFrame)
{
    _Game_engine_Assert (FrameCount != 1 && InWriteFrame < FrameCount,
                         "Index is out of range.");

    if (FrameCount != 1 && InWriteFrame < FrameCount)
    {
        WriteFrame = InWriteFrame;
        ReadFrame  = (InWriteFrame + FrameCount + 1) % FrameCount;
    }
    else if (FrameCount == 1)
    {
        WriteFrame = 0;
        ReadFrame  = 0;
    }
}

bool apemode::RenderPassResources::RecreateResourcesFor (apemode::GraphicsDevice &   InGraphicsNode,
                                                      apemode::RenderPass const & InRenderPass,
                                                      apemode::Swapchain **       ppInSwapchains,
                                                      uint32_t                 InSwapchainCount,
                                                      uint32_t                 InFrameCount,
                                                      uint32_t                 InColorWidth,
                                                      uint32_t                 InColorHeight,
                                                      VkClearValue             InColorClear,
                                                      uint32_t                 InDepthStencilWidth,
                                                      uint32_t                 InDepthStencilHeight,
                                                      VkClearValue             InDepthStencilClear,
                                                      bool                     bInReversedZ)
{
    if (_Game_engine_Unlikely (InRenderPass.pNode != &InGraphicsNode
                               || InRenderPass.pDesc == nullptr))
    {
        _Game_engine_Halt ("Render pass is not initialized, or logical device mismatch.");
        return false;
    }

    _Game_engine_Assert (InColorWidth > 0 && InColorHeight > 0, "Cannot be zero.");

    pRenderPass = &InRenderPass;
    RenderArea.offset = { 0, 0 };
    RenderArea.extent = { InColorWidth, InColorHeight };

    auto & RenderPassDesc = *InRenderPass.pDesc;

    _Game_engine_Assert (InFrameCount <= kFrameMaxCount,
                         "Frame count overlow (%u vs %u)",
                         InFrameCount,
                         kFrameMaxCount);

    FrameCount = InFrameCount;
    SetWriteFrame (0);

    for (uint32_t FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
        TextureViews[ FrameIdx ].reserve (RenderPassDesc.Attachments.size ());

    uint32_t AttachmentId = 0;
    for (auto & Attachment : RenderPassDesc.Attachments)
    {
        if (_Game_engine_Unlikely (ResourceReference::IsDepthStencilFormat (Attachment.format)))
        {
            _Game_engine_Assert (InDepthStencilWidth > 0 && InDepthStencilHeight > 0,
                                 "Cannot be zero.");

            ClearValues.push_back (InDepthStencilClear);

            for (uint32_t FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
            {
                DepthStencilAttachments[ FrameIdx ].push_back (
                    DepthStencilResourceView::MakeNewLinked ());
                _Game_engine_Assert (DepthStencilAttachments[ FrameIdx ].back (),
                                     "Out of system memory.");

                if (!DepthStencilAttachments[ FrameIdx ].back ()->RecreateResourcesFor (
                        InGraphicsNode,
                        Attachment.format,
                        InDepthStencilWidth,
                        InDepthStencilHeight,
                        bInReversedZ ? 0.0f : 1.0f,
                        0,
                        VK_IMAGE_LAYOUT_UNDEFINED))
                {
                    _Game_engine_Halt ("Failed to create depth-stencil view.");
                    return false;
                }

                DepthStencilAttachments[ FrameIdx ].back ()->ClearColor = InDepthStencilClear;

                TextureViews[ FrameIdx ].push_back (
                    DepthStencilAttachments[ FrameIdx ].back ().get ());
            }
        }
        else
        {
            uint32_t SwapchainId;
            if (RenderPassDesc.GetSwapchainAttachmentInfo (AttachmentId, SwapchainId))
            {
                _Game_engine_Assert (SwapchainId < InSwapchainCount, "Index is out of range.");

                std::vector<VkImage> & SwapchainBuffers
                    = ppInSwapchains[ SwapchainId ]->hBuffers;

                _Game_engine_Assert (SwapchainBuffers.size () == FrameCount,
                                     "Swapchain configuration does not match the provided one.");

                ClearValues.push_back (InColorClear);
                Scissors.push_back (VkRect2D{ { 0, 0 }, { InColorWidth, InColorHeight } });
                Viewports.push_back (VkViewport{ 0.0f,
                                                 0.0f,
                                                 static_cast<float> (InColorWidth),
                                                 static_cast<float> (InColorHeight),
                                                 0.0f,
                                                 1.0f });

                for (uint32_t FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
                {
                    ColorAttachments[ FrameIdx ].push_back (ColorResourceView::MakeNewLinked ());
                    _Game_engine_Assert (ColorAttachments[ FrameIdx ].back (),
                                         "Out of system memory.");

                    if (!ColorAttachments[ FrameIdx ].back ()->RecreateResourcesFor (
                            InGraphicsNode, Attachment.format, SwapchainBuffers[ FrameIdx ]))
                    {
                        _Game_engine_Halt ("Failed to create color view.");
                        return false;
                    }

                    ColorAttachments[ FrameIdx ].back ()->Width      = InColorWidth;
                    ColorAttachments[ FrameIdx ].back ()->Height     = InColorHeight;
                    ColorAttachments[ FrameIdx ].back ()->ClearColor = InColorClear;

                    TextureViews[ FrameIdx ].push_back (
                        ColorAttachments[ FrameIdx ].back ().get ());
                }
            }
            else
            {
                _Game_engine_Halt ("Not implemented.");

                ClearValues.push_back (InColorClear);
                for (uint32_t FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
                    TextureViews[ FrameIdx ].push_back (nullptr);
            }

        }

        ++AttachmentId;
    }

    FramebufferBuilder FramebufferBuilder;
    for (uint32_t FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
    {
        for (auto pTextureView : TextureViews[ FrameIdx ])
        {
            _Game_engine_Assert (pTextureView != nullptr, "Not initialized.");
            FramebufferBuilder.Attach (*pTextureView);
        }

        FramebufferBuilder.SetRenderPass (*GetRenderPass ());
        ppFramebuffers[ FrameIdx ] = FramebufferBuilder.RecreateFramebuffer (InGraphicsNode);

        if (ppFramebuffers[ FrameIdx ] == nullptr)
        {
            _Game_engine_Error ("Failed to create framebuffer.");
            return false;
        }

        FramebufferBuilder.Reset ();
    }

    _Game_engine_Assert (ClearValues.size () == RenderPassDesc.Attachments.size (),
                         "Should be equal.");

    return true;
}

void apemode::RenderPassResources::AdvanceFrame ()
{
    ReadFrame  = WriteFrame;
    WriteFrame = (WriteFrame + 1) % FrameCount;
}

uint32_t apemode::RenderPassResources::GetWriteFrame () const
{
    return WriteFrame;
}

uint32_t apemode::RenderPassResources::GetFrameCount () const
{
    return FrameCount;
}

uint32_t apemode::RenderPassResources::GetReadFrame () const
{
    return ReadFrame;
}

uint32_t apemode::RenderPassResources::GetAttachmentCount () const
{
    return _Get_collection_length_u (ClearValues);
}

apemode::RenderPass const * apemode::RenderPassResources::GetRenderPass () const
{
    return pRenderPass;
}

apemode::Framebuffer const * apemode::RenderPassResources::GetWriteFramebuffer () const
{
    return ppFramebuffers[ GetWriteFrame () ];
}

/// -------------------------------------------------------------------------------------------------------------------
/// RenderPassResources BeginEndScope
/// -------------------------------------------------------------------------------------------------------------------

apemode::RenderPassResources::BeginEndScope::BeginEndScope (CommandList &         CmdList,
                                                         RenderPassResources & Resources)
    : AssocCmdList (CmdList), AssocResources (Resources)
{
    _Game_engine_Assert (CmdList.bIsInBeginEndScope,
                         "Not started.");
    _Game_engine_Assert (Resources.GetRenderPass () != nullptr && Resources.GetWriteFramebuffer () != nullptr,
                         "Not initialized.");

    TInfoStruct<VkRenderPassBeginInfo> RenderPassBeginDesc;
    RenderPassBeginDesc->renderArea      = Resources.RenderArea;
    RenderPassBeginDesc->pClearValues    = Resources.ClearValues.data ();
    RenderPassBeginDesc->clearValueCount = _Get_collection_length_u (Resources.ClearValues);
    RenderPassBeginDesc->framebuffer     = *Resources.GetWriteFramebuffer ();
    RenderPassBeginDesc->renderPass      = *Resources.GetRenderPass ();

    vkCmdBeginRenderPass (CmdList,
                          RenderPassBeginDesc,
                          CmdList.IsDirect () ? VK_SUBPASS_CONTENTS_INLINE
                                              : VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

    //vkCmdSetViewport (CmdList, 0, _Get_collection_length_u (Resources.Viewports), Resources.Viewports.data ());
    //vkCmdSetScissor (CmdList, 0, _Get_collection_length_u (Resources.Scissors), Resources.Scissors.data ());

    CmdList.pRenderPass  = Resources.GetRenderPass ();
    CmdList.pFramebuffer = Resources.GetWriteFramebuffer ();
}

apemode::RenderPassResources::BeginEndScope::~BeginEndScope ()
{
    AssocCmdList.pRenderPass  = nullptr;
    AssocCmdList.pFramebuffer = nullptr;

    vkCmdEndRenderPass (AssocCmdList);
}
