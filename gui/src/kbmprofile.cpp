// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <kbmprofile.h>

#include <chiaki/controller.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace kbm
{

namespace
{

struct ActionInfo
{
	Action action;
	const char *name;
};

const ActionInfo action_infos[] = {
	{ Action::Cross, "cross" },
	{ Action::Circle, "circle" },
	{ Action::Square, "square" },
	{ Action::Triangle, "triangle" },
	{ Action::L1, "l1" },
	{ Action::R1, "r1" },
	{ Action::L2, "l2" },
	{ Action::R2, "r2" },
	{ Action::L3, "l3" },
	{ Action::R3, "r3" },
	{ Action::Options, "options" },
	{ Action::Create, "create" },
	{ Action::Touchpad, "touchpad" },
	{ Action::PS, "ps" },
	{ Action::DpadUp, "dpad_up" },
	{ Action::DpadDown, "dpad_down" },
	{ Action::DpadLeft, "dpad_left" },
	{ Action::DpadRight, "dpad_right" },
	{ Action::LsUp, "ls_up" },
	{ Action::LsDown, "ls_down" },
	{ Action::LsLeft, "ls_left" },
	{ Action::LsRight, "ls_right" },
	{ Action::RsUp, "rs_up" },
	{ Action::RsDown, "rs_down" },
	{ Action::RsLeft, "rs_left" },
	{ Action::RsRight, "rs_right" },
	{ Action::Walk, "walk" },
	{ Action::VehicleToggle, "vehicle_toggle" },
};

static_assert(sizeof(action_infos) / sizeof(action_infos[0]) == ActionCount, "every action needs a name");

std::string Trim(const std::string &s)
{
	size_t b = 0, e = s.size();
	while(b < e && std::isspace(static_cast<unsigned char>(s[b])))
		b++;
	while(e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
		e--;
	return s.substr(b, e - b);
}

std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

std::vector<std::string> SplitList(const std::string &s)
{
	std::vector<std::string> result;
	std::string item;
	std::istringstream stream(s);
	while(std::getline(stream, item, ','))
	{
		item = Trim(item);
		if(!item.empty())
			result.push_back(item);
	}
	return result;
}

// Strips an inline comment (" ;..." or " #...") from a value.
std::string StripInlineComment(const std::string &value)
{
	for(size_t i = 0; i < value.size(); i++)
	{
		if((value[i] == ';' || value[i] == '#') && (i == 0 || std::isspace(static_cast<unsigned char>(value[i - 1]))))
			return Trim(value.substr(0, i));
	}
	return Trim(value);
}

bool ParseDouble(const std::string &s, double *out)
{
	try
	{
		size_t pos = 0;
		double v = std::stod(s, &pos);
		if(pos != s.size() || !std::isfinite(v))
			return false;
		*out = v;
		return true;
	}
	catch(...)
	{
		return false;
	}
}

bool ParseBool(const std::string &s, bool *out)
{
	std::string l = Lower(s);
	if(l == "true" || l == "1" || l == "yes" || l == "on")
		*out = true;
	else if(l == "false" || l == "0" || l == "no" || l == "off")
		*out = false;
	else
		return false;
	return true;
}

int16_t ToAxis(double v)
{
	v = std::clamp(v, -1.0, 1.0);
	return static_cast<int16_t>(std::lround(v * 32767.0));
}

} // namespace

const char *ActionName(Action action)
{
	for(const auto &info : action_infos)
		if(info.action == action)
			return info.name;
	return "";
}

std::optional<Action> ParseActionName(const std::string &name)
{
	std::string l = Lower(Trim(name));
	for(const auto &info : action_infos)
		if(l == info.name)
			return info.action;
	return std::nullopt;
}

IniDocument IniDocument::Parse(const std::string &text)
{
	IniDocument doc;
	std::istringstream stream(text);
	std::string raw;
	std::string section;
	while(std::getline(stream, raw))
	{
		if(!raw.empty() && raw.back() == '\r')
			raw.pop_back();
		Line line;
		line.raw = raw;
		std::string t = Trim(raw);
		if(!t.empty() && t.front() == '[' && t.back() == ']')
			section = Lower(Trim(t.substr(1, t.size() - 2)));
		line.section = section;
		if(!t.empty() && t.front() != ';' && t.front() != '#' && t.front() != '[')
		{
			size_t eq = t.find('=');
			if(eq != std::string::npos)
			{
				line.key = Lower(Trim(t.substr(0, eq)));
				line.value = StripInlineComment(t.substr(eq + 1));
			}
		}
		doc.lines.push_back(line);
	}
	return doc;
}

std::optional<std::string> IniDocument::Get(const std::string &section, const std::string &key) const
{
	std::string s = Lower(section), k = Lower(key);
	for(const auto &line : lines)
		if(line.section == s && line.key == k)
			return line.value;
	return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> IniDocument::Entries(const std::string &section) const
{
	std::vector<std::pair<std::string, std::string>> result;
	std::string s = Lower(section);
	for(const auto &line : lines)
		if(line.section == s && !line.key.empty())
			result.emplace_back(line.key, line.value);
	return result;
}

void IniDocument::Set(const std::string &section, const std::string &key, const std::string &value)
{
	std::string s = Lower(section), k = Lower(key);
	for(auto &line : lines)
	{
		if(line.section == s && line.key == k)
		{
			line.value = value;
			line.raw = key + " = " + value;
			return;
		}
	}

	Line line;
	line.section = s;
	line.key = k;
	line.value = value;
	line.raw = key + " = " + value;

	// insert after the last line of the existing section
	size_t insert_at = lines.size();
	bool found = false;
	for(size_t i = 0; i < lines.size(); i++)
	{
		if(lines[i].section == s)
		{
			found = true;
			if(!Trim(lines[i].raw).empty())
				insert_at = i + 1;
		}
	}
	if(!found)
	{
		Line header;
		header.raw = "[" + section + "]";
		header.section = s;
		lines.push_back(header);
		insert_at = lines.size();
	}
	lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at), line);
}

std::string IniDocument::ToString() const
{
	std::string result;
	for(const auto &line : lines)
	{
		result += line.raw;
		result += '\n';
	}
	return result;
}

Profile ParseProfile(const std::string &text)
{
	Profile profile;
	IniDocument doc = IniDocument::Parse(text);

	auto read_double_in = [&](const char *section, const char *key, double *out, double min, double max) {
		auto v = doc.Get(section, key);
		if(!v)
			return;
		const std::string where = std::string("[") + section + "] " + key;
		double d;
		if(!ParseDouble(*v, &d))
		{
			profile.warnings.push_back(where + ": not a number: " + *v);
			return;
		}
		if(d < min || d > max)
		{
			profile.warnings.push_back(where + ": out of range, clamped: " + *v);
			d = std::clamp(d, min, max);
		}
		*out = d;
	};
	auto read_double = [&](const char *key, double *out, double min, double max) {
		read_double_in("mouse", key, out, min, max);
	};
	auto read_bool = [&](const char *section, const char *key, bool *out) {
		auto v = doc.Get(section, key);
		if(v && !ParseBool(*v, out))
			profile.warnings.push_back(std::string("[") + section + "] " + key + ": not a boolean: " + *v);
	};

	AimConfig &aim = profile.aim;
	read_double("sensitivity", &aim.sensitivity, 0.0, 100.0);
	read_double("exponent", &aim.exponent, 0.1, 5.0);
	read_double("anti_deadzone", &aim.anti_deadzone, 0.0, 0.9);
	read_double("max_output", &aim.max_output, 0.05, 1.0);
	read_double("smoothing_ms", &aim.smoothing_ms, 1.0, 200.0);
	read_double("y_scale", &aim.y_scale, 0.0, 10.0);
	read_double("ads_scale", &aim.ads_scale, 0.0, 10.0);
	read_double("walk_scale", &aim.walk_scale, 0.0, 1.0);
	read_double("update_interval_ms", &aim.update_interval_ms, 1.0, 50.0);
	read_bool("mouse", "invert_y", &aim.invert_y);
	read_bool("general", "click_to_capture", &profile.click_to_capture);

	SteerConfig &steer = profile.steer;
	read_double_in("steering", "start", &steer.start, 0.0, 1.0);
	read_double_in("steering", "ramp_ms", &steer.ramp_ms, 0.0, 5000.0);
	read_double_in("steering", "max", &steer.max, 0.05, 1.0);
	steer.start = std::min(steer.start, steer.max);

	auto read_bindings = [&](const char *section, std::vector<Binding> *out) {
		for(const auto &[key, value] : doc.Entries(section))
		{
			auto action = ParseActionName(key);
			if(!action)
			{
				profile.warnings.push_back(std::string("[") + section + "] unknown action: " + key);
				continue;
			}
			for(const auto &input : SplitList(value))
				out->push_back({ input, *action });
		}
	};
	read_bindings("bindings", &profile.bindings);
	read_bindings("vehicle", &profile.vehicle_bindings);
	return profile;
}

std::array<bool, ActionCount> HeldActions(const std::vector<int> &held_inputs,
		const BindingMap &on_foot, const BindingMap &vehicle, bool vehicle_mode)
{
	std::array<bool, ActionCount> held = {};
	for(int input : held_inputs)
	{
		auto it = vehicle_mode ? vehicle.find(input) : vehicle.end();
		if(it == vehicle.end())
		{
			it = on_foot.find(input);
			if(it == on_foot.end())
				continue;
		}
		for(Action action : it->second)
			held[static_cast<size_t>(action)] = true;
	}
	return held;
}

double SteerRamp::Update(double now_ms, int direction)
{
	if(direction != held_direction)
	{
		held_direction = direction;
		held_since_ms = now_ms;
	}
	if(!held_direction)
		return 0.0;
	double t = config.ramp_ms > 0.0 ? std::clamp((now_ms - held_since_ms) / config.ramp_ms, 0.0, 1.0) : 1.0;
	return held_direction * (config.start + (config.max - config.start) * t);
}

bool VehicleLayer::Update(bool toggle_held)
{
	bool pressed = toggle_held && !toggle_was_held;
	toggle_was_held = toggle_held;
	if(pressed)
		active = !active;
	return pressed;
}

void AimMapper::Reset()
{
	samples.clear();
	acc_dx = acc_dy = 0.0;
}

void AimMapper::Update(double now_ms, bool ads, int16_t *out_x, int16_t *out_y)
{
	if(acc_dx != 0.0 || acc_dy != 0.0)
		samples.push_back({ now_ms, acc_dx, acc_dy });
	acc_dx = acc_dy = 0.0;

	const double window = std::max(config.smoothing_ms, 1.0);
	while(!samples.empty() && samples.front().t <= now_ms - window)
		samples.pop_front();

	double sum_x = 0.0, sum_y = 0.0;
	for(const auto &s : samples)
	{
		sum_x += s.dx;
		sum_y += s.dy;
	}

	// mouse speed in counts per millisecond
	double vx = sum_x / window;
	double vy = sum_y / window * config.y_scale;
	if(config.invert_y)
		vy = -vy;

	double speed = std::hypot(vx, vy);
	if(speed < 1e-9)
	{
		*out_x = 0;
		*out_y = 0;
		return;
	}

	double scaled = speed * config.sensitivity * (ads ? config.ads_scale : 1.0);
	double magnitude = config.anti_deadzone + std::pow(scaled, config.exponent);
	magnitude = std::min(magnitude, config.max_output);

	*out_x = ToAxis(vx / speed * magnitude);
	*out_y = ToAxis(vy / speed * magnitude);
}

PadOutput ComputePad(const std::array<bool, ActionCount> &held, double walk_scale)
{
	auto h = [&](Action a) { return held[static_cast<size_t>(a)]; };
	PadOutput out;

	struct { Action action; uint32_t bit; } buttons[] = {
		{ Action::Cross, CHIAKI_CONTROLLER_BUTTON_CROSS },
		{ Action::Circle, CHIAKI_CONTROLLER_BUTTON_MOON },
		{ Action::Square, CHIAKI_CONTROLLER_BUTTON_BOX },
		{ Action::Triangle, CHIAKI_CONTROLLER_BUTTON_PYRAMID },
		{ Action::L1, CHIAKI_CONTROLLER_BUTTON_L1 },
		{ Action::R1, CHIAKI_CONTROLLER_BUTTON_R1 },
		{ Action::L3, CHIAKI_CONTROLLER_BUTTON_L3 },
		{ Action::R3, CHIAKI_CONTROLLER_BUTTON_R3 },
		{ Action::Options, CHIAKI_CONTROLLER_BUTTON_OPTIONS },
		{ Action::Create, CHIAKI_CONTROLLER_BUTTON_SHARE },
		{ Action::Touchpad, CHIAKI_CONTROLLER_BUTTON_TOUCHPAD },
		{ Action::PS, CHIAKI_CONTROLLER_BUTTON_PS },
		{ Action::DpadUp, CHIAKI_CONTROLLER_BUTTON_DPAD_UP },
		{ Action::DpadDown, CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN },
		{ Action::DpadLeft, CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT },
		{ Action::DpadRight, CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT },
	};
	for(const auto &b : buttons)
		if(h(b.action))
			out.buttons |= b.bit;

	out.l2 = h(Action::L2) ? 0xff : 0;
	out.r2 = h(Action::R2) ? 0xff : 0;

	// Opposite directions cancel out; diagonals are normalized to the unit circle.
	auto stick = [](bool up, bool down, bool left, bool right, double scale, int16_t *x, int16_t *y) {
		double fx = (right ? 1.0 : 0.0) - (left ? 1.0 : 0.0);
		double fy = (down ? 1.0 : 0.0) - (up ? 1.0 : 0.0);
		double len = std::hypot(fx, fy);
		if(len > 1.0)
		{
			fx /= len;
			fy /= len;
		}
		*x = ToAxis(fx * scale);
		*y = ToAxis(fy * scale);
	};
	stick(h(Action::LsUp), h(Action::LsDown), h(Action::LsLeft), h(Action::LsRight),
			h(Action::Walk) ? walk_scale : 1.0, &out.left_x, &out.left_y);
	stick(h(Action::RsUp), h(Action::RsDown), h(Action::RsLeft), h(Action::RsRight),
			1.0, &out.right_x, &out.right_y);
	return out;
}

const char *DefaultProfileText()
{
	return R"INI(; Keyboard + mouse profile for chiaki-ng (mouse aim mode).
;
; F8  - capture / release the mouse (mouse aim works while captured)
; F9  - sensitivity -10%,  F10 - sensitivity +10% (saved to this file)
; A left click on the stream window also captures the mouse (see click_to_capture).
; Edits to this file apply the next time the mouse is captured (press F8 twice).
;
; The layout below follows GTA 6's Default control scheme on PS5, with keys
; placed like GTA on PC. GTA 6 accelerates with R2 and brakes with L2, so W and S
; cannot be the same buttons on foot and in a car: [bindings] is the on-foot
; layer and [vehicle] switches W / S and a few other keys while you drive.
; "Vehicle controls" shows in the top-left corner while the vehicle layer is on.

[general]
click_to_capture = true

[mouse]
; Stick deflection (0..1) per mouse count per millisecond. The main setting.
sensitivity = 0.12
; Response curve: 1.0 = linear, above 1 = finer control for small movements.
exponent = 1.0
; Minimum stick deflection whenever the mouse moves (jumps over the game's deadzone).
; If the camera sticks on slow movements, raise it; if it jitters, lower it.
anti_deadzone = 0.18
max_output = 1.0
; Mouse speed smoothing window, ms. Larger = smoother, but the camera glides longer.
smoothing_ms = 12
; Vertical multiplier and inversion.
y_scale = 1.0
invert_y = false
; Sensitivity multiplier while L2 (aim) is held.
ads_scale = 1.0
; Left stick deflection while the "walk" modifier is held.
walk_scale = 0.45
update_interval_ms = 4

[steering]
; Keyboard steering (A / D) in the vehicle layer. A steering key starts at
; `start` and reaches `max` after being held for `ramp_ms`, so a tap nudges
; and a hold turns hard. ramp_ms = 0 jumps straight to max.
start = 0.3
ramp_ms = 400
max = 1.0

[bindings]
; action = key[, key ...]
; Keys:  A..Z, 0..9, F1..F12, Space, Tab, Enter, Esc, Backspace, Shift, Ctrl, Alt,
;        CapsLock, Up, Down, Left, Right, Insert, Delete, Home, End, PageUp, PageDown
; Mouse: MouseLeft, MouseRight, MouseMiddle, MouseBack, MouseForward, WheelUp, WheelDown
; F8, F9, F10 and F11 are reserved for the controls above and fullscreen.
;
; Actions: cross circle square triangle l1 r1 l2 r2 l3 r3 options create touchpad ps
;          dpad_up dpad_down dpad_left dpad_right ls_up ls_down ls_left ls_right
;          rs_up rs_down rs_left rs_right walk vehicle_toggle

; --- on foot (GTA 6 Default scheme) ---
ls_up = W
ls_down = S
ls_left = A
ls_right = D
; run (L3); press again to sprint
l3 = Shift
; slow walk (partial stick). Avoid Alt: on Windows, releasing Alt can open the window menu.
walk = CapsLock
; jump / climb; dodge and block in melee
cross = Space
; crouch (hold: rest / lean)
circle = Ctrl
; melee (hold: heavy attack)
square = R
; interact: doors, items, people; F also enters a vehicle (see vehicle_toggle)
triangle = E, F

; shoot (hold: grab / prime explosive) and aim
r2 = MouseLeft
l2 = MouseRight
; take cover
r1 = Q
; holster / draw (hold: weapon wheel)
l1 = Tab
; look behind (hold) / zoom while aiming; Shift + C is Focus (L3 + R3)
r3 = C, MouseMiddle

; phone (hold: WAiNK)
dpad_up = Up
; HUD / minimap (hold: quick navigation)
dpad_down = Down, Z
; radio (hold); switch shoulder while aiming
dpad_left = Left, MouseBack
; partner menu (hold), detonate explosives
dpad_right = Right, G, MouseForward

; map
touchpad = M
; pause menu
options = Esc, P
create = F12
ps = Home

; F enters a vehicle and switches to the [vehicle] layer; V switches layers by hand
; (use it if the layer gets out of step, e.g. F pressed with no car nearby).
vehicle_toggle = F, V

; keyboard camera control (fallback)
rs_up = I
rs_down = K
rs_left = J
rs_right = L

[vehicle]
; Only keys listed here change while the vehicle layer is on; a key listed here
; does only what it does here. Every other key keeps its on-foot action, so
; A / D steer, Space is the handbrake, Shift is horn / nitrous (L3), C looks behind.
; accelerate
r2 = W
; brake / reverse
l2 = S
; horn / nitrous
l3 = E
; headlights (tap) / duck (hold)
circle = H, Ctrl
; radio / vehicle menu (hold), turn signal left (tap)
dpad_left = Q, Left, MouseBack
; drive-by shoot
r1 = MouseLeft
; draw sidearm (tap), vehicle weapon (hold)
l1 = MouseRight
; F exits the vehicle and switches back to the on-foot layer
triangle = F
vehicle_toggle = F
)INI";
}

} // namespace kbm
