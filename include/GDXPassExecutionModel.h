#pragma once

#include <cstdint>

// Backend-neutrales Ausfuehrungsmodell fuer Render-Paesse.
// Beschreibt WAS ein Pass semantisch ist und WIE die Commands angeliefert werden,
// ohne DX11-/DX12-/Vulkan-Begriffe in den Core zu ziehen.

enum class GDXPassExecutionClass : uint8_t
{
    Graphics = 0,
    Shadow,
    Presentation,
    Compute,
};

enum class GDXCommandEncoding : uint8_t
{
    DrawQueue = 0,
    PassCommandList,
};

enum class GDXSubmissionOrder : uint8_t
{
    Sequential = 0,
    ExplicitDependencies,
};

struct GDXPassExecutionInfo
{
    GDXPassExecutionClass executionClass = GDXPassExecutionClass::Graphics;
    GDXCommandEncoding    commandEncoding = GDXCommandEncoding::DrawQueue;
    GDXSubmissionOrder    submissionOrder = GDXSubmissionOrder::Sequential;
    bool                  updatesFrameConstants = true;
    bool                  allowImplicitStatePromotion = true;
};
