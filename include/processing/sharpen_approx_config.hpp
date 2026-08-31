#pragma once

#include "approximate/evoapprox_adapter.hpp"

namespace image_processing
{

struct SharpenApproxConfig
{
    approximate::ApproxUnitId subtractTop;
    approximate::ApproxUnitId subtractBottom;
    approximate::ApproxUnitId subtractLeft;
    approximate::ApproxUnitId subtractRight;
};

SharpenApproxConfig createNormalConfig();

SharpenApproxConfig createImportantConfig();

SharpenApproxConfig createNonImportantConfig();

}