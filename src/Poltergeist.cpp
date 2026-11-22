// Poltergeist: a ghost that lives in your rack and moves real cables.
//
// The ghost can only touch the FENCE: the next 1-16 modules to the RIGHT of
// Poltergeist on the same row (never VCV Core modules such as Audio and MIDI,
// never itself). It only touches cables whose BOTH ends are inside the fence,
// so cables leaving the fence (to your mixer, to Audio) are always safe.
//
// Each HAUNT (trigger or button, CHANCE decides whether a trigger counts) does
// one piece of mischief, picked by the SWAP / WANDER / VANISH weights:
//   SWAP    two cables exchange their destinations (a->x, b->y  becomes a->y, b->x)
//   WANDER  one cable moves one of its ends to another socket in the fence
//   VANISH  one cable is pulled out and comes back after RETURN seconds
// EXORCISE (trigger or button) undoes everything the ghost did, newest first.
// Every mischief is also one Ctrl+Z step (right-click to turn that off).
//
// Threading: Rack only allows cables to be added or removed from the UI thread,
// so the audio engine (process) only counts triggers. The panel widget's step()
// (UI thread, once per screen frame) does the actual cable work.
#include "plugin.hpp"
#include <atomic>
#include <mutex>
#include <set>

// ---- LAYOUT in mm (same numbers in res/make_poltergeist.py) ----
static const float DISP_X = 5.0f, DISP_Y = 16.0f, DISP_W = 61.12f, DISP_H = 24.0f;
static const float K1_Y = 52.0f, K1_X[2] = {20.0f, 51.12f};
static const float K2_Y = 71.0f, K3_Y = 87.5f;
static const float COL3[3] = {13.0f, 35.56f, 58.12f};
static const float IN_Y = 104.0f;
static const float OUT_Y = 117.0f, OUT_X[2] = {24.0f, 47.12f};

static const int MAX_OPS = 64;

static float returnSeconds(float v) {
	return 0.1f * std::pow(200.f, v);   // 0.1 s .. 20 s
}

struct ReturnQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		float v = getValue();
		if (v >= 0.99f)
			return "never (until exorcised)";
		return string::f("%.1f s", returnSeconds(v));
	}
};

// One cable, remembered by its two ends so it can be put back later.
struct CableEnd {
	int64_t cableId = -1;
	int64_t outMod = -1;
	int outId = 0;
	int64_t inMod = -1;
	int inId = 0;
	NVGcolor color = nvgRGB(0xff, 0xff, 0xff);
};

enum Kind { K_SWAP, K_WANDER, K_VANISH };

// One piece of mischief: which cables the ghost removed and which it added.
struct Op {
	int kind = K_SWAP;
	std::vector<CableEnd> removed;
	std::vector<int64_t> added;
	bool waiting = false;     // VANISH: still out, waiting to return
	double returnAt = -1.0;   // system time to return (-1 = only on exorcise)
};

struct Poltergeist : Module {
	enum ParamId {
		CHANCE_PARAM, FENCE_PARAM,
		SWAP_PARAM, WANDER_PARAM, VANISH_PARAM,
		RETURN_PARAM, HAUNT_PARAM, EXORCISE_PARAM,
		PARAMS_LEN
	};
	enum InputId { HAUNT_INPUT, EXORCISE_INPUT, CHANCE_INPUT, INPUTS_LEN };
	enum OutputId { MISCHIEF_OUTPUT, HAUNTED_OUTPUT, OUTPUTS_LEN };
	enum LightId { HAUNT_LIGHT, EXORCISE_LIGHT, LIGHTS_LEN };

	// engine thread -> UI thread
	std::atomic<int> pendingHaunts{0};
	std::atomic<bool> pendingExorcise{false};
	// UI thread -> engine thread
	std::atomic<bool> mischiefFlag{false};
	std::atomic<int> hauntedCount{0};

	dsp::SchmittTrigger hauntTrig, exorciseTrig, hauntBtn, exorciseBtn;
	dsp::PulseGenerator mischiefPulse, hauntLightPulse, exorciseLightPulse;

	// UI-thread state (also saved with the patch)
	std::mutex opsMutex;
	std::vector<Op> ops;
	bool allowSelf = false;     // allow a module to be patched into itself
	bool useHistory = true;     // each mischief is one Ctrl+Z step
	int fenceShow = 0;          // 0 always, 1 while turning FENCE, 2 never

	Poltergeist() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(CHANCE_PARAM, 0.f, 1.f, 1.f, "Chance a HAUNT trigger does mischief", "%", 0.f, 100.f);
		ParamQuantity* fq = configParam(FENCE_PARAM, 1.f, 16.f, 4.f, "Fence: modules to the right the ghost may touch");
		fq->snapEnabled = true;
		configParam(SWAP_PARAM, 0.f, 1.f, 0.6f, "Swap weight", "%", 0.f, 100.f);
		configParam(WANDER_PARAM, 0.f, 1.f, 0.4f, "Wander weight", "%", 0.f, 100.f);
		configParam(VANISH_PARAM, 0.f, 1.f, 0.2f, "Vanish weight", "%", 0.f, 100.f);
		configParam<ReturnQuantity>(RETURN_PARAM, 0.f, 1.f, 0.45f, "Vanished cable returns after");
		configButton(HAUNT_PARAM, "Haunt now");
		configButton(EXORCISE_PARAM, "Exorcise (put every cable back)");
		configInput(HAUNT_INPUT, "Haunt trigger");
		configInput(EXORCISE_INPUT, "Exorcise trigger");
		configInput(CHANCE_INPUT, "Chance CV (0-10 V adds to the knob)");
		configOutput(MISCHIEF_OUTPUT, "Mischief trigger (fires when a cable moves)");
		configOutput(HAUNTED_OUTPUT, "Haunted (1 V per mischief not yet undone, up to 10 V)");
	}

	void onReset() override {
		pendingHaunts = 0;
		pendingExorcise = false;
	}

	void process(const ProcessArgs& args) override {
		float chance = clamp(params[CHANCE_PARAM].getValue() + inputs[CHANCE_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		if (hauntTrig.process(inputs[HAUNT_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (random::uniform() < chance && pendingHaunts.load() < 4) {
				pendingHaunts++;
				hauntLightPulse.trigger(0.08f);
			}
		}
		if (hauntBtn.process(params[HAUNT_PARAM].getValue())) {
			if (pendingHaunts.load() < 4)
				pendingHaunts++;
			hauntLightPulse.trigger(0.08f);
		}
		if (exorciseTrig.process(inputs[EXORCISE_INPUT].getVoltage(), 0.1f, 1.f)
		    || exorciseBtn.process(params[EXORCISE_PARAM].getValue())) {
			pendingExorcise = true;
			exorciseLightPulse.trigger(0.2f);
		}
		if (mischiefFlag.exchange(false))
			mischiefPulse.trigger(1e-3f);

		outputs[MISCHIEF_OUTPUT].setVoltage(mischiefPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[HAUNTED_OUTPUT].setVoltage(std::min(10.f, (float) hauntedCount.load()));
		lights[HAUNT_LIGHT].setBrightnessSmooth(hauntLightPulse.process(args.sampleTime) ? 1.f : 0.f, args.sampleTime);
		lights[EXORCISE_LIGHT].setBrightnessSmooth(exorciseLightPulse.process(args.sampleTime) ? 1.f : 0.f, args.sampleTime);
	}

	// ---- saving ----
	static json_t* endToJson(const CableEnd& e) {
		json_t* j = json_object();
		json_object_set_new(j, "id", json_integer(e.cableId));
		json_object_set_new(j, "outMod", json_integer(e.outMod));
		json_object_set_new(j, "outId", json_integer(e.outId));
		json_object_set_new(j, "inMod", json_integer(e.inMod));
		json_object_set_new(j, "inId", json_integer(e.inId));
		json_object_set_new(j, "color", json_string(color::toHexString(e.color).c_str()));
		return j;
	}
	static CableEnd endFromJson(json_t* j) {
		CableEnd e;
		e.cableId = json_integer_value(json_object_get(j, "id"));
		e.outMod = json_integer_value(json_object_get(j, "outMod"));
		e.outId = json_integer_value(json_object_get(j, "outId"));
		e.inMod = json_integer_value(json_object_get(j, "inMod"));
		e.inId = json_integer_value(json_object_get(j, "inId"));
		json_t* c = json_object_get(j, "color");
		if (c)
			e.color = color::fromHexString(json_string_value(c));
		return e;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "allowSelf", json_boolean(allowSelf));
		json_object_set_new(root, "useHistory", json_boolean(useHistory));
		json_object_set_new(root, "fenceShow", json_integer(fenceShow));
		json_t* opsJ = json_array();
		std::lock_guard<std::mutex> lock(opsMutex);
		for (const Op& op : ops) {
			json_t* o = json_object();
			json_object_set_new(o, "kind", json_integer(op.kind));
			json_object_set_new(o, "waiting", json_boolean(op.waiting));
			json_object_set_new(o, "forever", json_boolean(op.returnAt < 0.0));
			json_t* rem = json_array();
			for (const CableEnd& e : op.removed)
				json_array_append_new(rem, endToJson(e));
			json_object_set_new(o, "removed", rem);
			json_t* add = json_array();
			for (int64_t id : op.added)
				json_array_append_new(add, json_integer(id));
			json_object_set_new(o, "added", add);
			json_array_append_new(opsJ, o);
		}
		json_object_set_new(root, "ops", opsJ);
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "allowSelf"))) allowSelf = json_boolean_value(j);
		if ((j = json_object_get(root, "useHistory"))) useHistory = json_boolean_value(j);
		if ((j = json_object_get(root, "fenceShow"))) fenceShow = json_integer_value(j);
		std::lock_guard<std::mutex> lock(opsMutex);
		ops.clear();
		json_t* opsJ = json_object_get(root, "ops");
		if (!opsJ)
			return;
		double now = system::getTime();
		size_t i;
		json_t* o;
		json_array_foreach(opsJ, i, o) {
			Op op;
			op.kind = json_integer_value(json_object_get(o, "kind"));
			op.waiting = json_boolean_value(json_object_get(o, "waiting"));
			bool forever = json_boolean_value(json_object_get(o, "forever"));
			// A vanished cable that was waiting comes back a second after loading.
			op.returnAt = (op.waiting && !forever) ? now + 1.0 : -1.0;
			size_t k;
			json_t* e;
			json_array_foreach(json_object_get(o, "removed"), k, e)
				op.removed.push_back(endFromJson(e));
			json_array_foreach(json_object_get(o, "added"), k, e)
				op.added.push_back(json_integer_value(e));
			ops.push_back(op);
		}
		hauntedCount = (int) ops.size();
	}
};

// ---------------------------------------------------------------------------
// The ghost, drawn with nanovg: a dome, three wavy hems and two eyes.
static void drawGhost(NVGcontext* vg, float cx, float cy, float s, float alpha, float look, float t) {
	float hem = cy + s * 1.15f;
	float w = 2.f * s / 3.f;
	nvgBeginPath(vg);
	nvgMoveTo(vg, cx - s, cy);
	nvgArc(vg, cx, cy, s, M_PI, 0.f, NVG_CW);
	nvgLineTo(vg, cx + s, hem);
	for (int i = 0; i < 3; i++) {
		float x0 = cx + s - i * w;
		float lift = s * (0.32f + 0.12f * std::sin(t * 6.f + i * 2.1f));
		nvgQuadTo(vg, x0 - w / 2.f, hem - lift, x0 - w, hem);
	}
	nvgClosePath(vg);
	nvgFillColor(vg, nvgRGBAf(0.88f, 0.97f, 1.f, 0.92f * alpha));
	nvgFill(vg);
	nvgStrokeColor(vg, nvgRGBAf(0.61f, 1.f, 0.84f, 0.8f * alpha));
	nvgStrokeWidth(vg, std::max(0.6f, s * 0.08f));
	nvgStroke(vg);
	// eyes, pupils look toward the last cable the ghost touched
	float dx = std::cos(look) * s * 0.08f, dy = std::sin(look) * s * 0.08f;
	for (int k = -1; k <= 1; k += 2) {
		float ex = cx + k * s * 0.36f, ey = cy - s * 0.05f;
		nvgBeginPath(vg);
		nvgEllipse(vg, ex, ey, s * 0.2f, s * 0.26f);
		nvgFillColor(vg, nvgRGBAf(0.08f, 0.07f, 0.12f, alpha));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, ex + dx - s * 0.04f, ey + dy - s * 0.08f, s * 0.07f);
		nvgFillColor(vg, nvgRGBAf(1.f, 1.f, 1.f, 0.9f * alpha));
		nvgFill(vg);
	}
}

struct PoltergeistWidget;

// The screen: the ghost, what it last did, and the fence size.
struct PoltergeistDisplay : Widget {
	PoltergeistWidget* pw = NULL;
	void drawLayer(const DrawArgs& args, int layer) override;
};

struct PoltergeistWidget : ModuleWidget {
	// what the screen shows (UI thread only)
	std::string line1 = "SLEEPING";
	std::string line2 = "send a HAUNT trigger";
	int fenceCount = 0;
	int fenceCables = 0;
	float look = 0.f;
	double lastFenceTouch = -100.0;
	float lastFenceValue = -1.f;
	// fence rectangles and the flying ghost, in this widget's own coordinates
	std::vector<math::Rect> fenceRects;
	Vec flyFrom, flyTo;
	double flyStart = -100.0;

	PoltergeistWidget(Poltergeist* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Poltergeist.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		PoltergeistDisplay* d = createWidget<PoltergeistDisplay>(mm2px(Vec(DISP_X, DISP_Y)));
		d->box.size = mm2px(Vec(DISP_W, DISP_H));
		d->pw = this;
		addChild(d);

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(K1_X[0], K1_Y)), module, Poltergeist::CHANCE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(K1_X[1], K1_Y)), module, Poltergeist::FENCE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(COL3[0], K2_Y)), module, Poltergeist::SWAP_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(COL3[1], K2_Y)), module, Poltergeist::WANDER_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(COL3[2], K2_Y)), module, Poltergeist::VANISH_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(COL3[0], K3_Y)), module, Poltergeist::RETURN_PARAM));
		addParam(createLightParamCentered<VCVLightBezel<GreenLight>>(mm2px(Vec(COL3[1], K3_Y)), module,
			Poltergeist::HAUNT_PARAM, Poltergeist::HAUNT_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<RedLight>>(mm2px(Vec(COL3[2], K3_Y)), module,
			Poltergeist::EXORCISE_PARAM, Poltergeist::EXORCISE_LIGHT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL3[0], IN_Y)), module, Poltergeist::HAUNT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL3[1], IN_Y)), module, Poltergeist::EXORCISE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL3[2], IN_Y)), module, Poltergeist::CHANCE_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[0], OUT_Y)), module, Poltergeist::MISCHIEF_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[1], OUT_Y)), module, Poltergeist::HAUNTED_OUTPUT));
	}

	Poltergeist* pm() {
		return dynamic_cast<Poltergeist*>(module);
	}

	// ---- the fence: the next N modules to the right on the same row ----
	std::vector<ModuleWidget*> getFence(int n) {
		std::vector<ModuleWidget*> row;
		for (ModuleWidget* mw : APP->scene->rack->getModules()) {
			if (mw == this || !mw->module || !mw->model)
				continue;
			if (std::fabs(mw->box.pos.y - box.pos.y) > 1.f || mw->box.pos.x <= box.pos.x)
				continue;
			if (mw->model->plugin && mw->model->plugin->slug == "Core")
				continue;
			if (dynamic_cast<PoltergeistWidget*>(mw))
				continue;
			row.push_back(mw);
		}
		std::sort(row.begin(), row.end(), [](ModuleWidget* a, ModuleWidget* b) {
			return a->box.pos.x < b->box.pos.x;
		});
		if ((int) row.size() > n)
			row.resize(n);
		return row;
	}

	Vec rackPos(Widget* w, Vec local) {
		return w->getRelativeOffset(local, APP->scene->rack);
	}

	Vec toLocal(Vec rp) {
		return rp.minus(rackPos(this, Vec(0, 0)));
	}

	// ---- cable helpers (the same calls Rack's own undo history uses) ----
	CableWidget* addCable(history::ComplexAction* ca, const CableEnd& e) {
		Module* outM = APP->engine->getModule(e.outMod);
		Module* inM = APP->engine->getModule(e.inMod);
		if (!outM || !inM)
			return NULL;
		ModuleWidget* outW = APP->scene->rack->getModule(e.outMod);
		ModuleWidget* inW = APP->scene->rack->getModule(e.inMod);
		if (!outW || !inW || !outW->getOutput(e.outId) || !inW->getInput(e.inId))
			return NULL;
		engine::Cable* cable = new engine::Cable;
		if (e.cableId >= 0 && !APP->scene->rack->getCable(e.cableId))
			cable->id = e.cableId;   // reuse the old id so later undo steps still find it
		cable->outputModule = outM;
		cable->outputId = e.outId;
		cable->inputModule = inM;
		cable->inputId = e.inId;
		APP->engine->addCable(cable);
		CableWidget* cw = new CableWidget;
		cw->setCable(cable);
		cw->color = e.color;
		APP->scene->rack->addCable(cw);
		history::CableAdd* h = new history::CableAdd;
		h->setCable(cw);
		ca->push(h);
		return cw;
	}

	void removeCable(history::ComplexAction* ca, CableWidget* cw) {
		history::CableRemove* h = new history::CableRemove;
		h->setCable(cw);
		ca->push(h);
		APP->scene->rack->removeCable(cw);
		delete cw;
	}

	static CableEnd endOf(CableWidget* cw) {
		CableEnd e;
		engine::Cable* c = cw->getCable();
		e.cableId = c->id;
		e.outMod = c->outputModule->id;
		e.outId = c->outputId;
		e.inMod = c->inputModule->id;
		e.inId = c->inputId;
		e.color = cw->color;
		return e;
	}

	void finish(history::ComplexAction* ca, const char* name, bool useHistory) {
		ca->name = name;
		if (useHistory && !ca->isEmpty())
			APP->history->push(ca);
		else
			delete ca;
	}

	bool inputFree(int64_t modId, int inId) {
		ModuleWidget* mw = APP->scene->rack->getModule(modId);
		if (!mw)
			return false;
		PortWidget* pw = mw->getInput(inId);
		return pw && APP->scene->rack->getCablesOnPort(pw).empty();
	}

	std::string portName(int64_t modId, int portId, bool input) {
		Module* m = APP->engine->getModule(modId);
		if (!m || !m->model)
			return "?";
		std::string name;
		if (input && portId < (int) m->inputInfos.size())
			name = m->inputInfos[portId]->getName();
		if (!input && portId < (int) m->outputInfos.size())
			name = m->outputInfos[portId]->getName();
		std::string s = m->model->name + " " + name;
		if (s.size() > 26)
			s = s.substr(0, 25) + "~";
		return s;
	}

	void flyToInput(int64_t modId, int inId) {
		ModuleWidget* mw = APP->scene->rack->getModule(modId);
		if (!mw)
			return;
		PortWidget* p = mw->getInput(inId);
		if (!p)
			return;
		flyFrom = mm2px(Vec(DISP_X + 9.f, DISP_Y + 9.f));
		flyTo = toLocal(rackPos(p, p->box.size.div(2)));
		flyStart = system::getTime();
		Vec d = flyTo.minus(flyFrom);
		look = std::atan2(d.y, d.x);
	}

	// Put back what one piece of mischief changed. Cables the user has since
	// moved are left alone.
	void undoOp(history::ComplexAction* ca, Op& op) {
		for (int64_t id : op.added) {
			CableWidget* cw = APP->scene->rack->getCable(id);
			if (cw)
				removeCable(ca, cw);
		}
		for (const CableEnd& e : op.removed) {
			if (inputFree(e.inMod, e.inId))
				addCable(ca, e);
		}
	}

	bool selfOk(Poltergeist* m, int64_t outMod, int64_t inMod) {
		return m->allowSelf || outMod != inMod;
	}

	// ---- one piece of mischief ----
	void haunt(Poltergeist* m) {
		int n = (int) std::round(m->params[Poltergeist::FENCE_PARAM].getValue());
		std::vector<ModuleWidget*> fence = getFence(n);
		std::set<int64_t> ids;
		for (ModuleWidget* mw : fence)
			ids.insert(mw->module->id);

		std::vector<CableWidget*> cables;
		for (CableWidget* cw : APP->scene->rack->getCompleteCables()) {
			engine::Cable* c = cw->getCable();
			if (c && ids.count(c->outputModule->id) && ids.count(c->inputModule->id))
				cables.push_back(cw);
		}
		std::vector<std::pair<int64_t, int>> freeIns, outs;
		for (ModuleWidget* mw : fence) {
			for (PortWidget* p : mw->getInputs())
				if (APP->scene->rack->getCablesOnPort(p).empty())
					freeIns.push_back({mw->module->id, p->portId});
			for (PortWidget* p : mw->getOutputs())
				outs.push_back({mw->module->id, p->portId});
		}
		if (cables.empty()) {
			line1 = "BORED";
			line2 = fence.empty() ? "nothing to my right" : "no cables in the fence";
			return;
		}

		float w[3] = {
			m->params[Poltergeist::SWAP_PARAM].getValue() * (cables.size() >= 2 ? 1.f : 0.f),
			m->params[Poltergeist::WANDER_PARAM].getValue(),
			m->params[Poltergeist::VANISH_PARAM].getValue(),
		};
		float total = w[0] + w[1] + w[2];
		if (total <= 0.f) {
			line1 = "RESTING";
			line2 = "all weights at zero";
			return;
		}
		float r = random::uniform() * total;
		int kind = (r < w[0]) ? K_SWAP : (r < w[0] + w[1]) ? K_WANDER : K_VANISH;

		history::ComplexAction* ca = new history::ComplexAction;
		Op op;
		op.kind = kind;
		bool done = false;

		if (kind == K_SWAP) {
			for (int tries = 0; tries < 12 && !done; tries++) {
				CableWidget* a = cables[random::u32() % cables.size()];
				CableWidget* b = cables[random::u32() % cables.size()];
				if (a == b)
					continue;
				CableEnd ea = endOf(a), eb = endOf(b);
				if (ea.inMod == eb.inMod && ea.inId == eb.inId)
					continue;
				if (ea.outMod == eb.outMod && ea.outId == eb.outId)
					continue;
				if (!selfOk(m, ea.outMod, eb.inMod) || !selfOk(m, eb.outMod, ea.inMod))
					continue;
				removeCable(ca, a);
				removeCable(ca, b);
				CableEnd na = ea, nb = eb;
				na.cableId = -1; nb.cableId = -1;
				na.inMod = eb.inMod; na.inId = eb.inId;
				nb.inMod = ea.inMod; nb.inId = ea.inId;
				CableWidget* ca1 = addCable(ca, na);
				CableWidget* ca2 = addCable(ca, nb);
				op.removed = {ea, eb};
				if (ca1) op.added.push_back(ca1->getCable()->id);
				if (ca2) op.added.push_back(ca2->getCable()->id);
				line1 = "SWAPPED";
				line2 = portName(ea.outMod, ea.outId, false) + " > " + portName(eb.inMod, eb.inId, true);
				flyToInput(eb.inMod, eb.inId);
				done = true;
			}
		}
		else if (kind == K_WANDER) {
			for (int tries = 0; tries < 12 && !done; tries++) {
				CableWidget* a = cables[random::u32() % cables.size()];
				CableEnd ea = endOf(a), na = ea;
				na.cableId = -1;
				bool moveInput = !freeIns.empty() && (outs.size() < 2 || random::uniform() < 0.5f);
				if (moveInput) {
					auto t = freeIns[random::u32() % freeIns.size()];
					na.inMod = t.first; na.inId = t.second;
				}
				else {
					auto t = outs[random::u32() % outs.size()];
					na.outMod = t.first; na.outId = t.second;
					if (na.outMod == ea.outMod && na.outId == ea.outId)
						continue;
				}
				if (!selfOk(m, na.outMod, na.inMod))
					continue;
				removeCable(ca, a);
				CableWidget* c1 = addCable(ca, na);
				op.removed = {ea};
				if (c1) op.added.push_back(c1->getCable()->id);
				line1 = moveInput ? "WANDERED" : "CROSSED OVER";
				line2 = portName(na.outMod, na.outId, false) + " > " + portName(na.inMod, na.inId, true);
				flyToInput(na.inMod, na.inId);
				done = true;
			}
		}
		else {
			CableWidget* a = cables[random::u32() % cables.size()];
			CableEnd ea = endOf(a);
			flyToInput(ea.inMod, ea.inId);
			removeCable(ca, a);
			op.removed = {ea};
			op.waiting = true;
			float rv = m->params[Poltergeist::RETURN_PARAM].getValue();
			op.returnAt = (rv >= 0.99f) ? -1.0 : system::getTime() + returnSeconds(rv);
			line1 = "VANISHED";
			line2 = portName(ea.inMod, ea.inId, true);
			done = true;
		}

		if (!done) {
			delete ca;
			line1 = "HESITATED";
			line2 = "nothing safe to move";
			return;
		}
		finish(ca, "poltergeist mischief", m->useHistory);
		{
			std::lock_guard<std::mutex> lock(m->opsMutex);
			m->ops.push_back(op);
			if ((int) m->ops.size() > MAX_OPS)
				m->ops.erase(m->ops.begin());   // the oldest mischief is forgotten
			m->hauntedCount = (int) m->ops.size();
		}
		m->mischiefFlag = true;
	}

	void exorcise(Poltergeist* m) {
		history::ComplexAction* ca = new history::ComplexAction;
		{
			std::lock_guard<std::mutex> lock(m->opsMutex);
			for (auto it = m->ops.rbegin(); it != m->ops.rend(); ++it)
				undoOp(ca, *it);
			m->ops.clear();
			m->hauntedCount = 0;
		}
		finish(ca, "exorcise poltergeist", m->useHistory);
		line1 = "EXORCISED";
		line2 = "every cable is back";
		flyStart = -100.0;
	}

	void returnVanished(Poltergeist* m, double now) {
		history::ComplexAction* ca = NULL;
		std::lock_guard<std::mutex> lock(m->opsMutex);
		for (auto it = m->ops.begin(); it != m->ops.end();) {
			if (it->waiting && it->returnAt >= 0.0 && now >= it->returnAt) {
				if (!ca)
					ca = new history::ComplexAction;
				undoOp(ca, *it);
				if (!it->removed.empty()) {
					line1 = "RETURNED";
					line2 = portName(it->removed[0].inMod, it->removed[0].inId, true);
					flyToInput(it->removed[0].inMod, it->removed[0].inId);
				}
				it = m->ops.erase(it);
			}
			else {
				++it;
			}
		}
		m->hauntedCount = (int) m->ops.size();
		if (ca)
			finish(ca, "poltergeist return", m->useHistory);
	}

	void step() override {
		ModuleWidget::step();
		Poltergeist* m = pm();
		if (!m)
			return;
		double now = system::getTime();

		// fence outline (rack coordinates -> ours)
		float fv = m->params[Poltergeist::FENCE_PARAM].getValue();
		if (fv != lastFenceValue) {
			if (lastFenceValue >= 0.f)
				lastFenceTouch = now;
			lastFenceValue = fv;
		}
		std::vector<ModuleWidget*> fence = getFence((int) std::round(fv));
		fenceRects.clear();
		std::set<int64_t> ids;
		for (ModuleWidget* mw : fence) {
			fenceRects.push_back(math::Rect(toLocal(rackPos(mw, Vec(0, 0))), mw->box.size));
			ids.insert(mw->module->id);
		}
		fenceCount = (int) fence.size();
		fenceCables = 0;
		bool dragging = false;
		for (CableWidget* cw : APP->scene->rack->getCables()) {
			if (!cw->isComplete()) {
				dragging = true;
				continue;
			}
			engine::Cable* c = cw->getCable();
			if (c && ids.count(c->outputModule->id) && ids.count(c->inputModule->id))
				fenceCables++;
		}

		if (m->isBypassed()) {
			m->pendingHaunts = 0;
			m->pendingExorcise = false;
			return;
		}
		// never pull cables out from under the user's hand
		if (dragging)
			return;

		if (m->pendingExorcise.exchange(false)) {
			m->pendingHaunts = 0;
			exorcise(m);
			return;
		}
		returnVanished(m, now);
		if (m->pendingHaunts.load() > 0) {
			m->pendingHaunts--;
			haunt(m);   // at most one piece of mischief per screen frame
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		ModuleWidget::drawLayer(args, layer);
		Poltergeist* m = pm();
		if (layer != 1 || !m)
			return;
		NVGcontext* vg = args.vg;
		double now = system::getTime();

		// the fence: a faint ghost-green outline round the modules it may touch
		float fenceAlpha = 0.f;
		if (m->fenceShow == 0)
			fenceAlpha = 0.35f;
		float since = (float) (now - lastFenceTouch);
		if (m->fenceShow != 2 && since < 2.5f)
			fenceAlpha = std::max(fenceAlpha, 0.9f * (1.f - since / 2.5f) + 0.1f);
		if (fenceAlpha > 0.f) {
			for (const math::Rect& r : fenceRects) {
				nvgBeginPath(vg);
				nvgRoundedRect(vg, r.pos.x + 1.5f, r.pos.y + 1.5f, r.size.x - 3.f, r.size.y - 3.f, 4.f);
				nvgFillColor(vg, nvgRGBAf(0.61f, 1.f, 0.84f, 0.05f * fenceAlpha));
				nvgFill(vg);
				nvgStrokeColor(vg, nvgRGBAf(0.61f, 1.f, 0.84f, fenceAlpha));
				nvgStrokeWidth(vg, 1.5f);
				nvgStroke(vg);
			}
		}

		// the ghost flying out to the cable it just touched, then fading
		float ft = (float) (now - flyStart);
		const float FLY = 0.7f, FADE = 0.9f;
		if (ft >= 0.f && ft < FLY + FADE) {
			float u = std::min(1.f, ft / FLY);
			float e = u * u * (3.f - 2.f * u);
			Vec p = flyFrom.plus(flyTo.minus(flyFrom).mult(e));
			p.y += std::sin(u * M_PI) * -30.f + std::sin(ft * 9.f) * 2.f;
			float alpha = (ft < FLY) ? 1.f : 1.f - (ft - FLY) / FADE;
			drawGhost(vg, p.x, p.y - 14.f, 9.f, alpha, look, ft);
		}
	}

	void appendContextMenu(Menu* menu) override {
		Poltergeist* m = pm();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("The ghost only touches cables with both ends inside the fence"));
		menu->addChild(createMenuItem("Exorcise now", "", [=]() { m->pendingExorcise = true; }));
		menu->addChild(createBoolPtrMenuItem("Allow patching a module into itself", "", &m->allowSelf));
		menu->addChild(createBoolPtrMenuItem("Each mischief is a Ctrl+Z step", "", &m->useHistory));
		menu->addChild(createIndexPtrSubmenuItem("Show fence",
			{"Always", "While turning FENCE", "Never"}, &m->fenceShow));
	}
};

void PoltergeistDisplay::drawLayer(const DrawArgs& args, int layer) {
	if (layer != 1) {
		Widget::drawLayer(args, layer);
		return;
	}
	NVGcontext* vg = args.vg;
	float w = box.size.x, h = box.size.y;
	double now = system::getTime();
	float t = (float) now;

	std::string l1 = "SLEEPING", l2 = "send a HAUNT trigger";
	int fc = 4, cc = 3, haunted = 0;
	float look = 0.f;
	bool flying = false;
	if (pw && pw->module) {
		l1 = pw->line1;
		l2 = pw->line2;
		fc = pw->fenceCount;
		cc = pw->fenceCables;
		look = pw->look;
		Poltergeist* m = pw->pm();
		haunted = m ? m->hauntedCount.load() : 0;
		float ft = (float) (now - pw->flyStart);
		flying = ft >= 0.f && ft < 0.7f;
	}
	else {
		l1 = "POLTERGEIST";
		l2 = "moves your cables";
	}

	// ghost at home (hidden while it is out flying)
	float gs = h * 0.24f;
	float gx = mm2px(9.f), gy = h * 0.42f + std::sin(t * 2.2f) * 1.5f;
	if (!flying)
		drawGhost(vg, gx, gy, gs, 1.f, look, t);
	else {
		nvgBeginPath(vg);
		nvgEllipse(vg, gx, gy + gs * 1.3f, gs * 0.8f, gs * 0.15f);
		nvgFillColor(vg, nvgRGBAf(0.61f, 1.f, 0.84f, 0.25f));
		nvgFill(vg);
	}

	std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
	if (!font)
		return;
	nvgFontFaceId(vg, font->handle);
	float tx = mm2px(18.5f);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
	nvgFontSize(vg, mm2px(4.6f));
	nvgFillColor(vg, nvgRGB(0x9C, 0xFF, 0xD6));
	nvgText(vg, tx, mm2px(7.6f), l1.c_str(), NULL);
	nvgFontSize(vg, mm2px(2.9f));
	nvgFillColor(vg, nvgRGB(0xED, 0xE7, 0xFF));
	nvgTextBox(vg, tx, mm2px(12.4f), w - tx - mm2px(1.5f), l2.c_str(), NULL);
	nvgFillColor(vg, nvgRGB(0x9C, 0x97, 0xB5));
	std::string l3 = string::f("FENCE %d  CABLES %d  HAUNTED %d", fc, cc, haunted);
	nvgText(vg, mm2px(1.6f), h - mm2px(1.5f), l3.c_str(), NULL);
	Widget::drawLayer(args, layer);
}

Model* modelPoltergeist = createModel<Poltergeist, PoltergeistWidget>("Poltergeist");
