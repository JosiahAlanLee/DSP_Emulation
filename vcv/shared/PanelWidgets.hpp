// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <rack.hpp>
#include <vector>

namespace dspemu { namespace panel {

// Compact named selectors, matching Vastish's panel controls. Parameter IDs and
// quantities belong to the module; this only presents their existing choices.
struct Choice : rack::app::ParamWidget {
    std::vector<std::string> labels;
    int previewIndex = 0;
    void draw(const DrawArgs& args) override {
        auto* q = getParamQuantity();
        int index = q ? int(std::round(q->getValue() - q->minValue)) : previewIndex;
        std::string label = index >= 0 && index < int(labels.size()) ? labels[index] : "—";
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(29,39,45)); nvgFill(args.vg);
        auto font = APP->window->uiFont;
        if (font) {
            nvgSave(args.vg);
            nvgScissor(args.vg, 3, 1, box.size.x-15, box.size.y-2);
            nvgFontFaceId(args.vg, font->handle); nvgFontSize(args.vg, 10);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, nvgRGB(143,220,196));
            nvgText(args.vg, (box.size.x-8)/2, box.size.y/2, label.c_str(), nullptr);
            nvgRestore(args.vg);
        }
        nvgBeginPath(args.vg); nvgMoveTo(args.vg, box.size.x-10, box.size.y/2-2);
        nvgLineTo(args.vg, box.size.x-4, box.size.y/2-2);
        nvgLineTo(args.vg, box.size.x-7, box.size.y/2+2); nvgClosePath(args.vg);
        nvgFillColor(args.vg, nvgRGB(127,148,156)); nvgFill(args.vg);
        ParamWidget::draw(args);
    }
    void onButton(const rack::event::Button& e) override {
        auto* q = getParamQuantity();
        if (q && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            auto* menu = rack::createMenu();
            for (size_t i = 0; i < labels.size(); ++i) {
                float value = q->minValue + i;
                menu->addChild(rack::createCheckMenuItem(labels[i], "",
                    [q,value]{ return std::round(q->getValue()) == value; },
                    [q,value]{
                        float previous = q->getValue();
                        q->setValue(value);
                        if (q->getValue() != previous && APP->history) {
                            auto* action = new rack::history::ParamChange;
                            action->moduleId = q->module->id; action->paramId = q->paramId;
                            action->oldValue = previous; action->newValue = q->getValue();
                            APP->history->push(action);
                        }
                    }));
            }
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
};

}} // namespace dspemu::panel
