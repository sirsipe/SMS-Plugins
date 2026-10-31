#include "MidichopperView.hpp"

#include "ViewRenderers.hpp"
#include "DPF/AnalogMaterials.hpp"

namespace midichopper::ui {

void draw(DGL_NAMESPACE::NanoVG& canvas, const ViewState& state)
{
    namespace uiLayout = layout;
    using namespace rendering;
    const ViewRenderContext ctx{canvas, state};
    sms::ui::dpf::drawChassis(ctx.canvas, {0.0f, 0.0f, static_cast<float>(uiLayout::canvasWidth),
                                           static_cast<float>(uiLayout::canvasHeight)});
    if (ctx.state.background != nullptr && ctx.state.background->isValid()) {
        ctx.canvas.beginPath();
        ctx.canvas.roundedRect(0.0f, 0.0f, uiLayout::canvasWidth, uiLayout::canvasHeight, 18.0f);
        ctx.canvas.fillPaint(
            ctx.canvas.imagePattern(0.0f, 0.0f, uiLayout::canvasWidth, uiLayout::canvasHeight, 0.0f,
                                    *ctx.state.background, sms::ui::dpf::theme().backgroundAlpha));
        ctx.canvas.fill();
    }
    sms::ui::dpf::drawScrew(ctx.canvas, 20.0f, 20.0f);
    sms::ui::dpf::drawScrew(ctx.canvas, uiLayout::canvasWidth - 20.0f, 20.0f);
    sms::ui::dpf::drawScrew(ctx.canvas, 20.0f, uiLayout::canvasHeight - 20.0f);
    sms::ui::dpf::drawScrew(ctx.canvas, uiLayout::canvasWidth - 20.0f,
                            uiLayout::canvasHeight - 20.0f);
    drawLevelFaders(ctx);

    ctx.canvas.save();
    ctx.canvas.translate(uiLayout::contentOffsetX, 0.0f);
    drawMainHeader(ctx);
    if (ctx.state.chopEditorMode)
        drawCutEditorView(ctx);
    else if (ctx.state.editorMode)
        drawSampleEditorView(ctx);
    else
        drawMainView(ctx);
    drawMainFooter(ctx);
    drawMenus(ctx);
    ctx.canvas.restore();
}

} // namespace midichopper::ui
