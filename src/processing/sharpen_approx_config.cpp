#include "processing/sharpen_approx_config.hpp"

namespace image_processing
{

SharpenApproxConfig createNormalConfig()
{
    return SharpenApproxConfig{
        approximate::ApproxUnitId::Add12se5QT,
        approximate::ApproxUnitId::Add12se5QT,
        approximate::ApproxUnitId::Add12se5QT,
        approximate::ApproxUnitId::Add12se5QT
    };
}

SharpenApproxConfig createImportantConfig()
{
    return SharpenApproxConfig{
        approximate::ApproxUnitId::Add12se5SB,
        approximate::ApproxUnitId::Add12se5SB,
        approximate::ApproxUnitId::Add12se5SB,
        approximate::ApproxUnitId::Add12se5SB
    };
}

SharpenApproxConfig createNonImportantConfig()
{
    return SharpenApproxConfig{
        approximate::ApproxUnitId::Add12se5QT,
        approximate::ApproxUnitId::Add12se5QT,
        approximate::ApproxUnitId::Add12se5QT,
        approximate::ApproxUnitId::Add12se5QT
    };
}

}