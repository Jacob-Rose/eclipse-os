"""A python mirror of eanim::AutomationCurve, so a curve drawn on the desk
plays back exactly as it will on the relic.

The C++ side is src/lib/eanim/automation_curve.* and it is the authority:
same key cap, same sorted insert, same clamp-outside-the-span evaluate, same
"two keys at one time is a step and the later one wins", and the same easing
table (src/lib/external/easing.h, ported function for function). If the two
ever disagree, this file is the one that is wrong.

Nothing in here touches tk - the editor draws this, the tests exercise it.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Callable, List

# ---------------------------------------------------------------------------
# The easing table.
#
# Ported from src/lib/external/easing.cpp (nicolausYes/easing-functions),
# keeping that file's exact arithmetic - including its slightly odd constants
# (sin(1.5707963*t), the /255 expo) - because matching the sculpture matters
# more than matching a textbook. Names are the easing_functions enum entries,
# in enum order, so an index here is the value a C++ curve would store.
# ---------------------------------------------------------------------------

_PI = 3.1415926545


def _in_sine(t: float) -> float:
    return math.sin(1.5707963 * t)


def _out_sine(t: float) -> float:
    return 1.0 + math.sin(1.5707963 * (t - 1.0))


def _in_out_sine(t: float) -> float:
    return 0.5 * (1.0 + math.sin(3.1415926 * (t - 0.5)))


def _in_quad(t: float) -> float:
    return t * t


def _out_quad(t: float) -> float:
    return t * (2.0 - t)


def _in_out_quad(t: float) -> float:
    return 2.0 * t * t if t < 0.5 else t * (4.0 - 2.0 * t) - 1.0


def _in_cubic(t: float) -> float:
    return t * t * t


def _out_cubic(t: float) -> float:
    u = t - 1.0
    return 1.0 + u * u * u


def _in_out_cubic(t: float) -> float:
    if t < 0.5:
        return 4.0 * t * t * t
    # The C++ is `1 + (--t) * (2 * (--t)) * (2 * t)`: two unsequenced
    # decrements in one expression, which is undefined behaviour. The pinned
    # g++ applies both before any of the reads, so every operand sees t-2 and
    # the upper half comes out as 1 + 4(t-2)^3 - checked against the compiled
    # binary, which really does dive to -12.5 at the midpoint. Mirrored here
    # because the editor's job is to show what the rig will do, not what the
    # textbook says InOutCubic is.
    u = t - 2.0
    return 1.0 + 4.0 * u * u * u


def _in_quart(t: float) -> float:
    t = t * t
    return t * t


def _out_quart(t: float) -> float:
    u = (t - 1.0) * (t - 1.0)
    return 1.0 - u * u


def _in_out_quart(t: float) -> float:
    if t < 0.5:
        t = t * t
        return 8.0 * t * t
    u = (t - 1.0) * (t - 1.0)
    return 1.0 - 8.0 * u * u


def _in_quint(t: float) -> float:
    t2 = t * t
    return t * t2 * t2


def _out_quint(t: float) -> float:
    u = t - 1.0
    t2 = u * u
    return 1.0 + u * t2 * t2


def _in_out_quint(t: float) -> float:
    if t < 0.5:
        t2 = t * t
        return 16.0 * t * t2 * t2
    u = t - 1.0
    t2 = u * u
    return 1.0 + 16.0 * u * t2 * t2


def _in_expo(t: float) -> float:
    return (2.0 ** (8.0 * t) - 1.0) / 255.0


def _out_expo(t: float) -> float:
    return 1.0 - 2.0 ** (-8.0 * t)


def _in_out_expo(t: float) -> float:
    if t < 0.5:
        return (2.0 ** (16.0 * t) - 1.0) / 510.0
    return 1.0 - 0.5 * 2.0 ** (-16.0 * (t - 0.5))


def _in_circ(t: float) -> float:
    return 1.0 - math.sqrt(max(1.0 - t, 0.0))


def _out_circ(t: float) -> float:
    return math.sqrt(max(t, 0.0))


def _in_out_circ(t: float) -> float:
    if t < 0.5:
        return (1.0 - math.sqrt(max(1.0 - 2.0 * t, 0.0))) * 0.5
    return (1.0 + math.sqrt(max(2.0 * t - 1.0, 0.0))) * 0.5


def _in_back(t: float) -> float:
    return t * t * (2.70158 * t - 1.70158)


def _out_back(t: float) -> float:
    u = t - 1.0
    return 1.0 + u * u * (2.70158 * u + 1.70158)


def _in_out_back(t: float) -> float:
    if t < 0.5:
        return t * t * (7.0 * t - 2.5) * 2.0
    u = t - 1.0
    return 1.0 + u * u * 2.0 * (7.0 * u + 2.5)


def _in_elastic(t: float) -> float:
    t2 = t * t
    return t2 * t2 * math.sin(t * _PI * 4.5)


def _out_elastic(t: float) -> float:
    t2 = (t - 1.0) * (t - 1.0)
    return 1.0 - t2 * t2 * math.cos(t * _PI * 4.5)


def _in_out_elastic(t: float) -> float:
    if t < 0.45:
        t2 = t * t
        return 8.0 * t2 * t2 * math.sin(t * _PI * 9.0)
    if t < 0.55:
        return 0.5 + 0.75 * math.sin(t * _PI * 4.0)
    t2 = (t - 1.0) * (t - 1.0)
    return 1.0 - 8.0 * t2 * t2 * math.sin(t * _PI * 9.0)


# The bounce trio calls abs() on a double with only <cmath> and <map>
# included, so it lands on the int abs from <cstdlib>: the sine truncates to
# zero (or to +/-1 where rounding hits exactly 1.0) before the abs. Compiled,
# InBounce is flat zero, OutBounce is flat one, and InOutBounce is a step at
# the midpoint - verified against the pinned g++. _int_abs reproduces that.


def _int_abs(value: float) -> float:
    return float(abs(int(value)))


def _in_bounce(t: float) -> float:
    return 2.0 ** (6.0 * (t - 1.0)) * _int_abs(math.sin(t * _PI * 3.5))


def _out_bounce(t: float) -> float:
    return 1.0 - 2.0 ** (-6.0 * t) * _int_abs(math.cos(t * _PI * 3.5))


def _in_out_bounce(t: float) -> float:
    if t < 0.5:
        return 8.0 * 2.0 ** (8.0 * (t - 1.0)) * _int_abs(math.sin(t * _PI * 7.0))
    return 1.0 - 8.0 * 2.0 ** (-8.0 * t) * _int_abs(math.sin(t * _PI * 7.0))


#: enum order from easing.h - the index of a name here is its C++ enum value.
EASING_NAMES: List[str] = [
    "EaseInSine", "EaseOutSine", "EaseInOutSine",
    "EaseInQuad", "EaseOutQuad", "EaseInOutQuad",
    "EaseInCubic", "EaseOutCubic", "EaseInOutCubic",
    "EaseInQuart", "EaseOutQuart", "EaseInOutQuart",
    "EaseInQuint", "EaseOutQuint", "EaseInOutQuint",
    "EaseInExpo", "EaseOutExpo", "EaseInOutExpo",
    "EaseInCirc", "EaseOutCirc", "EaseInOutCirc",
    "EaseInBack", "EaseOutBack", "EaseInOutBack",
    "EaseInElastic", "EaseOutElastic", "EaseInOutElastic",
    "EaseInBounce", "EaseOutBounce", "EaseInOutBounce",
]

_EASING_FUNCTIONS: List[Callable[[float], float]] = [
    _in_sine, _out_sine, _in_out_sine,
    _in_quad, _out_quad, _in_out_quad,
    _in_cubic, _out_cubic, _in_out_cubic,
    _in_quart, _out_quart, _in_out_quart,
    _in_quint, _out_quint, _in_out_quint,
    _in_expo, _out_expo, _in_out_expo,
    _in_circ, _out_circ, _in_out_circ,
    _in_back, _out_back, _in_out_back,
    _in_elastic, _out_elastic, _in_out_elastic,
    _in_bounce, _out_bounce, _in_out_bounce,
]

#: What the editor offers for a segment. "linear" is the AutomationKey default
#: (bUseEasing false), which is why it is a name here and not an enum entry
#: there.
LINEAR = "linear"
SEGMENT_SHAPES: List[str] = [LINEAR] + EASING_NAMES


def apply_easing(name: str, alpha: float) -> float:
    """`alpha` through the named easing; linear (or an unknown name) unchanged."""
    try:
        return _EASING_FUNCTIONS[EASING_NAMES.index(name)](alpha)
    except ValueError:
        return alpha


@dataclass
class CurveKey:
    """One key, shaping the segment that leaves it - see eanim::AutomationKey."""

    time: float = 0.0
    value: float = 0.0
    #: an EASING_NAMES entry, or LINEAR for a straight segment
    easing: str = LINEAR


class Curve:
    """The desk's copy of eanim::AutomationCurve.

    Values are kept 0..1 here - a curve on the desk is a weight, mapped onto
    whichever knob it is aimed at - but nothing below enforces that; evaluate
    is range-agnostic, exactly like the C++.
    """

    #: eanim::AutomationCurve::kMaxKeys. The editor honours the cap so a curve
    #: built here always fits on the sculpture.
    MAX_KEYS = 8

    def __init__(self) -> None:
        self.keys: List[CurveKey] = []

    def add_key(self, time: float, value: float, easing: str = LINEAR) -> bool:
        """Adds a key, keeping the list sorted by time. False when full."""
        if len(self.keys) >= self.MAX_KEYS:
            return False

        key = CurveKey(time=time, value=value, easing=easing)
        index = len(self.keys)
        while index > 0 and self.keys[index - 1].time > time:
            index -= 1
        self.keys.insert(index, key)
        return True

    def remove_key(self, key: CurveKey) -> None:
        if key in self.keys:
            self.keys.remove(key)

    def resort(self) -> None:
        """Restores time order after a drag moved a key past its neighbour.

        Stable, so two keys at one time keep their order - and with it which
        one "wins" the step, same as the C++ insert would have left them.
        """
        self.keys.sort(key=lambda key: key.time)

    def clear(self) -> None:
        self.keys = []

    @property
    def duration(self) -> float:
        return self.keys[-1].time if self.keys else 0.0

    def evaluate(self, seconds: float) -> float:
        """The value at `seconds` - AutomationCurve::evaluate, line for line."""
        keys = self.keys
        count = len(keys)

        if count == 0:
            return 0.0
        if count == 1 or seconds <= keys[0].time:
            return keys[0].value
        if seconds >= keys[-1].time:
            return keys[-1].value

        segment = 0
        while segment < count - 2 and seconds >= keys[segment + 1].time:
            segment += 1

        from_key = keys[segment]
        to_key = keys[segment + 1]

        span = to_key.time - from_key.time
        if span <= 0.0:
            # two keys at the same time is a step, and the later one wins
            return to_key.value

        alpha = (seconds - from_key.time) / span
        alpha = apply_easing(from_key.easing, alpha)

        return from_key.value + (to_key.value - from_key.value) * alpha

    def to_cpp(self, name: str = "curve") -> str:
        """The curve as the addKey calls that rebuild it on a relic.

        This is the whole round trip: draw it on the desk, paste this into a
        state, and AutomationCurveTrigger plays the same shape the editor did.
        """
        lines = [f"{name}.clear();"]
        for key in self.keys:
            if key.easing == LINEAR:
                lines.append(f"{name}.addKey({key.time:.3f}f, {key.value:.3f}f);")
            else:
                lines.append(
                    f"{name}.addKey({key.time:.3f}f, {key.value:.3f}f, "
                    f"easing_functions::{key.easing});")
        return "\n".join(lines)


def example_hit() -> Curve:
    """The three-key hit from automation_curve.h's own docs: snap up, sit,
    trail away. What the editor opens on, so the first play press already
    shows a shape worth having.
    """
    curve = Curve()
    curve.add_key(0.0, 0.0, "EaseOutCubic")
    curve.add_key(0.35, 1.0, "EaseInOutSine")
    curve.add_key(1.6, 0.0)
    return curve
