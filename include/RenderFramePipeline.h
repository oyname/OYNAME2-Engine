#pragma once

#include "RenderViewData.h"
#include "RenderQueue.h"
#include "RenderGatherSystem.h"
#include "RenderPassTargetDesc.h"
#include "BackendRenderPassDesc.h"

#include <vector>
#include <string>
#include <cstdint>

struct ViewExecutionStats
{
    ViewCullingStats graphicsCulling{};
    ViewCullingStats shadowCulling{};
    uint32_t drawCalls = 0u;
    uint32_t renderCommands = 0u;
    uint32_t lightCount = 0u;
    uint32_t debugBoundsDraws = 0u;
    uint32_t debugFrustumDraws = 0u;
    bool graphicsPassExecuted = false;
    bool shadowPassExecuted = false;
    bool presentationExecuted = false;
    bool countedAsRenderTargetView = false;

    void Reset()
    {
        graphicsCulling = {};
        shadowCulling = {};
        drawCalls = 0u;
        renderCommands = 0u;
        lightCount = 0u;
        debugBoundsDraws = 0u;
        debugFrustumDraws = 0u;
        graphicsPassExecuted = false;
        shadowPassExecuted = false;
        presentationExecuted = false;
        countedAsRenderTargetView = false;
    }
};

struct PreparedViewData
{
    RenderViewData graphicsView{};
    RenderViewData shadowView{};
    RenderGatherOptions gatherOptions{};
    FrameData frame{};
    bool shadowEnabled = false;
    RenderPassClearDesc clearDesc{};
    RenderPassTargetDesc graphicsTargetDesc{};

    void Reset()
    {
        graphicsView = {};
        shadowView = {};
        gatherOptions = {};
        frame = {};
        shadowEnabled = false;
        clearDesc = {};
        graphicsTargetDesc = {};
    }
};

struct PreparedPassExecution
{
    bool enabled = false;
    BackendRenderPassDesc desc{};
    bool appendGraphicsVisibleSet = false;
    bool appendShadowVisibleSet = false;
    bool sortQueueBeforeExecute = false;

    void Reset()
    {
        enabled = false;
        desc = {};
        appendGraphicsVisibleSet = false;
        appendShadowVisibleSet = false;
        sortQueueBeforeExecute = false;
    }
};

struct PreparedPostProcessExecution
{
    bool enabled = false;
    TextureHandle sceneTexture = TextureHandle::Invalid();

    void Reset()
    {
        enabled = false;
        sceneTexture = TextureHandle::Invalid();
    }
};

struct PreparedPresentationExecution
{
    bool presentAfterExecute = false;
    PreparedPostProcessExecution postProcess{};

    void Reset()
    {
        presentAfterExecute = false;
        postProcess.Reset();
    }
};

struct PreparedExecuteData
{
    // NOTE: lights and frame constants are updated once per view in a dedicated
    // pre-pass inside ExecutePreparedFrame, not inside individual node execution.
    // This removes the mutable flag that previously coupled Prepare and Execute.
    PreparedPassExecution shadowPass{};
    PreparedPassExecution graphicsPass{};
    RenderQueue shadowQueue{};
    RenderQueue graphicsQueue{};
    PreparedPresentationExecution presentation{};

    void Reset()
    {
        shadowPass.Reset();
        graphicsPass.Reset();
        shadowQueue.Clear();
        graphicsQueue.Clear();
        presentation.Reset();
    }
};

struct ViewPassExecutionData
{
    PreparedViewData prepared{};
    PreparedExecuteData execute{};
    ViewExecutionStats stats{};
    VisibleSet graphicsVisibleSet{};
    VisibleSet shadowVisibleSet{};
    std::vector<RenderGatherSystem::GatherChunkResult> graphicsGatherChunks{};
    std::vector<RenderGatherSystem::GatherChunkResult> shadowGatherChunks{};
    RenderQueue opaqueQueue{};
    RenderQueue transparentQueue{};
    RenderQueue shadowQueue{};

    void Reset()
    {
        prepared.Reset();
        execute.Reset();
        stats.Reset();
        graphicsVisibleSet = {};
        shadowVisibleSet = {};
        graphicsGatherChunks.clear();
        shadowGatherChunks.clear();
        opaqueQueue.Clear();
        transparentQueue.Clear();
        shadowQueue.Clear();
    }

    RenderQueue BuildGraphicsQueue() const
    {
        RenderQueue queue;
        queue.commands = opaqueQueue.commands;
        queue.commands.insert(queue.commands.end(), transparentQueue.commands.begin(), transparentQueue.commands.end());
        return queue;
    }
};

enum class PreparedFrameGraphResourceKind : uint8_t
{
    None = 0,
    BackbufferColor = 1,
    MainSceneColor = 2,
    ShadowMap = 3,
    RenderTargetColor = 4
};

enum class PreparedFrameGraphResourceAccess : uint8_t
{
    None = 0,
    Read = 1,
    Write = 2
};

struct PreparedFrameGraphResourceRef
{
    PreparedFrameGraphResourceKind kind = PreparedFrameGraphResourceKind::None;
    PreparedFrameGraphResourceAccess access = PreparedFrameGraphResourceAccess::None;
    TextureHandle texture = TextureHandle::Invalid();
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    uint32_t scopeId = 0u;

    void Reset()
    {
        kind = PreparedFrameGraphResourceKind::None;
        access = PreparedFrameGraphResourceAccess::None;
        texture = TextureHandle::Invalid();
        renderTarget = RenderTargetHandle::Invalid();
        scopeId = 0u;
    }
};

enum class PreparedFrameGraphNodeKind : uint8_t
{
    RenderTargetShadow = 0,
    RenderTargetGraphics = 1,
    MainShadow = 2,
    MainGraphics = 3,
    MainPresentation = 4
};

struct PreparedFrameGraphNode
{
    PreparedFrameGraphNodeKind kind = PreparedFrameGraphNodeKind::MainGraphics;

    // Used during Prepare phase (dependency build) — not touched during Execute.
    ViewPassExecutionData* view = nullptr;

    // Typed Execute-phase interface:
    //   executeInput  — readonly snapshot of what this node must execute.
    //   statsOutput   — writeonly target for execution results.
    // Both are set in BuildPreparedFrameGraph immediately after view is assigned.
    // Execute code reads from executeInput and writes to statsOutput only,
    // never through the mutable view pointer.
    const PreparedExecuteData* executeInput = nullptr;
    ViewExecutionStats*        statsOutput  = nullptr;

    uint32_t viewIndex = 0u;
    bool enabled = false;
    bool countedAsRenderTargetView = false;
    PreparedFrameGraphResourceRef readResource{};
    PreparedFrameGraphResourceRef writeResource{};
    std::vector<uint32_t> dependencies{};

    void Reset()
    {
        kind = PreparedFrameGraphNodeKind::MainGraphics;
        view = nullptr;
        executeInput = nullptr;
        statsOutput  = nullptr;
        viewIndex = 0u;
        enabled = false;
        countedAsRenderTargetView = false;
        readResource.Reset();
        writeResource.Reset();
        dependencies.clear();
    }
};

struct PreparedFrameGraphValidation
{
    bool valid = true;
    std::vector<std::string> errors{};

    void Reset()
    {
        valid = true;
        errors.clear();
    }
};

struct PreparedFrameGraph
{
    std::vector<PreparedFrameGraphNode> nodes{};
    PreparedFrameGraphValidation validation{};
    std::vector<uint32_t> executionOrder{};

    void Reset()
    {
        nodes.clear();
        validation.Reset();
        executionOrder.clear();
    }
};

struct RendererFramePipelineData
{
    FrameData frameSnapshot{};
    ViewPassExecutionData mainView{};
    std::vector<ViewPassExecutionData> rttViews{};
    PreparedFrameGraph frameGraph{};

    void Reset()
    {
        frameSnapshot = {};
        mainView.Reset();
        rttViews.clear();
        frameGraph.Reset();
    }
};

using RenderFramePipelineData = RendererFramePipelineData;
