// Minimal Nation: one big knob (Haer knob design 241), one input, one output.
// The input is copied to the output untouched (all polyphony channels).
// The knob is stored with the patch but changes nothing. It is there to look at.
#include "plugin.hpp"

// ---- LAYOUT in mm (same numbers in res/make_panel.py) ----
static const float KNOB_X = 30.48f, KNOB_Y = 54.0f;
static const float IN_X = 17.0f, OUT_X = 43.96f, JACK_Y = 104.0f;

struct MinimalNation : Module {
	enum ParamId { KNOB_PARAM, PARAMS_LEN };
	enum InputId { IN_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	MinimalNation() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(KNOB_PARAM, 0.f, 10.f, 5.f, "Nothing", "", 0.f, 1.f);
		configInput(IN_INPUT, "In");
		configOutput(OUT_OUTPUT, "Out (same as in)");
		configBypass(IN_INPUT, OUT_OUTPUT);
	}

	void process(const ProcessArgs& args) override {
		int channels = inputs[IN_INPUT].getChannels();
		outputs[OUT_OUTPUT].setChannels(channels);
		for (int c = 0; c < channels; c++)
			outputs[OUT_OUTPUT].setVoltage(inputs[IN_INPUT].getVoltage(c), c);
	}
};

// Knob 241: a fixed face (white disc, black ring, shaded ball lit from above)
// with a turning black bar and red dot drawn on top.
struct Knob241 : app::SvgKnob {
	widget::SvgWidget* bg;
	Knob241() {
		minAngle = -0.83f * M_PI;
		maxAngle = 0.83f * M_PI;
		bg = new widget::SvgWidget;
		fb->addChildBelow(bg, tw);
		bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/Knob241-bg.svg")));
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/Knob241-fg.svg")));
		shadow->opacity = 0.08f;
	}
};

struct MinimalNationWidget : ModuleWidget {
	MinimalNationWidget(MinimalNation* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/MinimalNation.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<Knob241>(mm2px(Vec(KNOB_X, KNOB_Y)), module, MinimalNation::KNOB_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(IN_X, JACK_Y)), module, MinimalNation::IN_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X, JACK_Y)), module, MinimalNation::OUT_OUTPUT));
	}
};

Model* modelMinimalNation = createModel<MinimalNation, MinimalNationWidget>("MinimalNation");
