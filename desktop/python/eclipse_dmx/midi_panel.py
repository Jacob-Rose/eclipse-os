"""The MIDI mapping editor: a band down the right of the desk.

Folded away until asked for, like the curve editor - toggled from the button
band or [m] - and beside the rig rather than over it, because the whole point
of binding a pad is watching the cue (and the visual behind it) land when you
hit it.

The panel owns the `MappingSet` and its file. The viewer owns the traffic: it
drains the monitor's events in its pump and offers each one here first (for
learn), then to the dispatcher. Nothing in this file touches a socket or the
show - every action goes through midi_map's registry, which is what keeps the
editor generic: a new action registered there grows a form here, unedited.

The form is drawn from the ActionSpec's fields, which is the load-bearing
trick. The editor has no idea what a favslot is; it knows the action asked
for an int called "slot".
"""

from __future__ import annotations

import tkinter as tk
from pathlib import Path
from tkinter import filedialog
from typing import Callable, Dict, List, Optional

from .midi_map import (
    ACTIONS,
    Action,
    MODES,
    TRIGGER_KINDS,
    FieldSpec,
    Mapping,
    MappingSet,
    MidiEvent,
    fold_kind,
)

# the viewer's palette - keep in step with viewer.py (importing it from there
# would be circular: the viewer imports this)
PANEL = "#14181d"
PANEL_EDGE = "#2a323b"
TEXT = "#c8d0d8"
TEXT_DIM = "#6b7783"
TEXT_WARN = "#e8a33d"
BUTTON_BG = "#232a32"
BUTTON_BG_ACTIVE = "#3d6ea5"
BUTTON_FG = "#c8d0d8"
LIST_BG = "#0e1115"

FONT = ("Consolas", 9)
FONT_SMALL = ("Consolas", 8)


def _button(parent, text, command, **kw) -> tk.Button:
    return tk.Button(
        parent, text=text, font=FONT, command=command,
        bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
        activeforeground=BUTTON_FG, relief="flat", padx=6, pady=2,
        highlightthickness=0, borderwidth=0, **kw)


class _ActionRow:
    """The widgets for one action block, in the order the editor drew them.

    Only the variables are kept: the frames are owned by `_actions_frame` and
    destroyed wholesale on the next rebuild, so holding them would be holding
    references to widgets that are already gone.
    """

    __slots__ = ("action_var", "param_vars")

    def __init__(self, action_var: tk.StringVar,
                 param_vars: Dict[str, tk.StringVar]) -> None:
        self.action_var = action_var
        self.param_vars = param_vars


class MidiMapPanel:
    """The band: a list of mappings, and a form for the selected one."""

    WIDTH = 330

    def __init__(
        self,
        parent: tk.Widget,
        mappings: MappingSet,
        path: Optional[Path],
        default_dir: Path,
        on_status: Callable[[str], None],
        on_open_midi: Callable[[], None],
    ) -> None:
        self.mappings = mappings
        #: where save writes without asking; set by load/save-as
        self.path = path
        #: where the dialogs open: <config dir>/midimaps
        self.default_dir = default_dir
        self._say = on_status
        self._on_open_midi = on_open_midi

        self._selected: Optional[Mapping] = None
        self._learning = False
        self._finding = False
        self._dirty = False
        #: guards the editor's variable traces while the editor itself is
        #: being written to - the same trick as the viewer's param panel
        self._writing = False

        self.frame = tk.Frame(parent, bg=PANEL, width=self.WIDTH,
                              highlightbackground=PANEL_EDGE, highlightthickness=1)
        # fixed width: the desk gets the rest, and a panel that reflows when
        # a long scene name is typed is a panel that fights the mouse
        self.frame.pack_propagate(False)

        self._build_header()
        self._build_list()
        self._build_editor()
        self._build_footer()

        self._refresh_list()
        self._select_index(0 if self.mappings.mappings else None)

    # -- construction ------------------------------------------------------

    def _build_header(self) -> None:
        header = tk.Frame(self.frame, bg=PANEL)
        header.pack(fill="x", padx=6, pady=(6, 2))

        self._title = tk.Label(header, text="midi map", bg=PANEL, fg=TEXT, font=FONT)
        self._title.pack(side="left")

        _button(header, "load", self._load_dialog).pack(side="right", padx=2)
        _button(header, "save as", self._save_as_dialog).pack(side="right", padx=2)
        _button(header, "save", self.save).pack(side="right", padx=2)

    def _build_list(self) -> None:
        box = tk.Frame(self.frame, bg=PANEL)
        box.pack(fill="x", padx=6)

        # exportselection off, or every click into an Entry below silently
        # clears the selection and the form goes blank under the cursor
        self._list = tk.Listbox(
            box, height=8, font=FONT, exportselection=False,
            bg=LIST_BG, fg=TEXT, selectbackground=BUTTON_BG_ACTIVE,
            selectforeground=BUTTON_FG, highlightthickness=0, borderwidth=0,
            activestyle="none")
        self._list.pack(side="left", fill="x", expand=True)
        self._list.bind("<<ListboxSelect>>", lambda e: self._on_select())

        scroll = tk.Scrollbar(box, command=self._list.yview)
        scroll.pack(side="right", fill="y")
        self._list.configure(yscrollcommand=scroll.set)

        row = tk.Frame(self.frame, bg=PANEL)
        row.pack(fill="x", padx=6, pady=(2, 4))
        _button(row, "add", self._add).pack(side="left", padx=2)
        _button(row, "dup", self._duplicate).pack(side="left", padx=2)
        _button(row, "del", self._delete).pack(side="left", padx=2)

        # The way in from an empty map. The editor's own learn button rebinds
        # the mapping already selected, and it lives *in* the editor - which is
        # hidden when nothing is selected, so on a map with no mappings yet
        # there was no learn button on screen at all. This one is always there
        # and makes the mapping it is about to bind.
        self._add_learn_button = _button(row, "+ learn", self._add_and_learn)
        self._add_learn_button.pack(side="right", padx=2)

        # Learn read backwards: hit the pad, and the mapping it fires is the
        # one selected. The question at a surface with eighty pads on it is
        # usually "what is this one bound to", and answering it by reading
        # `ch=1 note 81` off a row and hunting for the pad is the wrong way
        # round. See take_find.
        self._find_button = _button(row, "find", self._toggle_find)
        self._find_button.pack(side="right", padx=2)

    def _build_editor(self) -> None:
        self._editor = tk.Frame(self.frame, bg=PANEL)
        self._editor.pack(fill="x", padx=6)

        def dim_label(parent, text) -> tk.Label:
            return tk.Label(parent, text=text, bg=PANEL, fg=TEXT_DIM, font=FONT)

        def entry(parent, var, width=20) -> tk.Entry:
            widget = tk.Entry(
                parent, textvariable=var, width=width, font=FONT,
                bg=LIST_BG, fg=TEXT, insertbackground=TEXT,
                relief="flat", highlightthickness=1,
                highlightbackground=PANEL_EDGE, highlightcolor=BUTTON_BG_ACTIVE)
            widget.bind("<FocusOut>", lambda e: self._commit())
            widget.bind("<Return>", lambda e: self._commit())
            return widget

        def option(parent, var, choices, command=None) -> tk.OptionMenu:
            widget = tk.OptionMenu(parent, var, *choices,
                                   command=command or (lambda _v: self._commit()))
            widget.configure(bg=BUTTON_BG, fg=BUTTON_FG, font=FONT,
                             activebackground=BUTTON_BG_ACTIVE, relief="flat",
                             highlightthickness=0, borderwidth=0, indicatoron=False,
                             padx=6, pady=2)
            widget["menu"].configure(bg=BUTTON_BG, fg=BUTTON_FG, font=FONT,
                                     activebackground=BUTTON_BG_ACTIVE, borderwidth=0)
            return widget

        # label + enabled
        row = tk.Frame(self._editor, bg=PANEL)
        row.pack(fill="x", pady=1)
        dim_label(row, "label ").pack(side="left")
        self._label_var = tk.StringVar()
        entry(row, self._label_var, width=16).pack(side="left")
        self._enabled_var = tk.BooleanVar(value=True)
        tk.Checkbutton(
            row, text="on", variable=self._enabled_var, command=self._commit,
            bg=PANEL, fg=TEXT_DIM, font=FONT, selectcolor=LIST_BG,
            activebackground=PANEL, activeforeground=TEXT,
            highlightthickness=0).pack(side="left", padx=4)

        # The pad's colour, for a controller with lamps. A palette index
        # rather than a swatch: the numbers are the controller's own, printed
        # in its manual, and a colour picker here would be this desk inventing
        # a second name for something the hardware has already named.
        dim_label(row, "lamp").pack(side="left", padx=(6, 0))
        self._colour_var = tk.StringVar(value="41")
        entry(row, self._colour_var, width=4).pack(side="left", padx=2)

        # Which tab this row lives on. Blank means every tab, which is what a
        # map with no pages is and what the tabs and arrows themselves are.
        row = tk.Frame(self._editor, bg=PANEL)
        row.pack(fill="x", pady=1)
        dim_label(row, "page  ").pack(side="left")
        self._page_var = tk.StringVar()
        entry(row, self._page_var, width=16).pack(side="left")
        dim_label(row, " blank = always").pack(side="left")

        # the trigger
        row = tk.Frame(self._editor, bg=PANEL)
        row.pack(fill="x", pady=1)
        dim_label(row, "when  ").pack(side="left")
        self._kind_var = tk.StringVar(value="note")
        option(row, self._kind_var, TRIGGER_KINDS).pack(side="left", padx=(0, 2))
        self._number_var = tk.StringVar(value="60")
        entry(row, self._number_var, width=4).pack(side="left", padx=2)
        dim_label(row, "ch").pack(side="left")
        self._channel_var = tk.StringVar(value="0")
        entry(row, self._channel_var, width=3).pack(side="left", padx=2)
        self._learn_button = _button(row, "learn", self._toggle_learn)
        self._learn_button.pack(side="left", padx=6)

        row = tk.Frame(self._editor, bg=PANEL)
        row.pack(fill="x", pady=1)
        dim_label(row, "mode  ").pack(side="left")
        self._mode_var = tk.StringVar(value="press")
        option(row, self._mode_var, MODES).pack(side="left")

        # the actions, and the form each one asks for. A frame rather than a
        # fixed row: a mapping holds a list, and the editor grows a block per
        # entry - see _rebuild_actions.
        self._action_labels = {spec.label: key for key, spec in ACTIONS.items()}
        self._actions_frame = tk.Frame(self._editor, bg=PANEL)
        self._actions_frame.pack(fill="x", pady=(6, 1))
        self._action_rows: List[_ActionRow] = []

        row = tk.Frame(self._editor, bg=PANEL)
        row.pack(fill="x", pady=(0, 4))
        self._add_action_button = _button(row, "+ do", self._add_action)
        self._add_action_button.pack(side="left")

        self._entry_factory = entry
        self._option_factory = option
        self._dim_label = dim_label

    def _build_footer(self) -> None:
        tk.Frame(self.frame, bg=PANEL_EDGE, height=1).pack(fill="x", padx=6, pady=2)

        row = tk.Frame(self.frame, bg=PANEL)
        row.pack(fill="x", padx=6, pady=(0, 2))
        _button(row, "open midi", self._on_open_midi).pack(side="left")

        #: the last message heard and the last thing a mapping did - the
        #: proof the wire is alive, which is the first question at a desk
        self._heard = tk.Label(self.frame, text="in: -", bg=PANEL, fg=TEXT_DIM,
                               font=FONT_SMALL, anchor="w", justify="left")
        self._heard.pack(fill="x", padx=8)
        self._did = tk.Label(self.frame, text="", bg=PANEL, fg=TEXT_DIM,
                             font=FONT_SMALL, anchor="w", justify="left")
        self._did.pack(fill="x", padx=8, pady=(0, 6))

    # -- the action blocks, drawn from the specs ---------------------------

    def _rebuild_actions(self) -> None:
        """One block per action on the selected mapping: a chooser, the form
        that action declares, and a way to take it off the row.

        Rebuilt whole whenever the *shape* changes - an action added, removed
        or switched to another kind - and left alone while text is typed into
        it. Cheap enough at this size, and it means there is one code path
        that turns a list of actions into widgets rather than one for each
        way the list can change.
        """
        for child in self._actions_frame.winfo_children():
            child.destroy()
        self._action_rows = []

        mapping = self._selected
        if mapping is None:
            return

        removable = len(mapping.actions) > 1
        for index, action in enumerate(mapping.actions):
            block = tk.Frame(self._actions_frame, bg=PANEL)
            block.pack(fill="x", pady=(0, 2))

            head = tk.Frame(block, bg=PANEL)
            head.pack(fill="x", pady=1)
            # "do" on the first, "and" on the rest: the list reads as a
            # sentence, and the eye can find where one action stops.
            self._dim_label(head, "do    " if index == 0 else "and   ").pack(side="left")

            action_var = tk.StringVar()
            spec = action.spec
            action_var.set(spec.label if spec is not None else action.key)
            self._option_factory(
                head, action_var, list(self._action_labels),
                command=lambda _v, i=index: self._on_action_kind(i)).pack(side="left")

            if removable:
                # Never offered on the last one. A mapping with no actions is
                # a trigger that does nothing, which the file format does not
                # have a way to spell - and `del` already removes the row.
                _button(head, "−",
                        lambda i=index: self._remove_action(i)).pack(side="right", padx=2)

            param_vars: Dict[str, tk.StringVar] = {}
            if spec is not None:
                for fld in spec.fields:
                    row = tk.Frame(block, bg=PANEL)
                    row.pack(fill="x", pady=1)
                    self._dim_label(row, f"  {fld.label:<7}").pack(side="left")
                    var = tk.StringVar()
                    var.set(str(action.params.get(fld.name, fld.default)))
                    param_vars[fld.name] = var
                    width = 6 if fld.kind in ("int", "float") else 22
                    self._entry_factory(row, var, width=width).pack(side="left")

            self._action_rows.append(_ActionRow(action_var, param_vars))

    # -- selection ---------------------------------------------------------

    def _on_select(self) -> None:
        # Clicking another row while armed would bind the next pad to whatever
        # was clicked, which is not what the click meant. Disarmed here rather
        # than carried, because a learn aimed at the wrong mapping is worse
        # than one that has to be asked for twice.
        if self._learning:
            self._stop_learn()
            self._say("midi learn: cancelled")
        # Same for find: a click is the answer to the question find was
        # asking, so leaving it armed would move the selection again on the
        # next pad hit.
        if self._finding:
            self._stop_find()
            self._say("midi find: cancelled")
        selection = self._list.curselection()
        self._select_index(selection[0] if selection else None, from_list=True)

    def _select_index(self, index: Optional[int], from_list: bool = False) -> None:
        rows = self.mappings.mappings
        if index is None or not rows:
            self._selected = None
            self._editor.pack_forget()
            return
        index = max(0, min(index, len(rows) - 1))
        self._selected = rows[index]
        if not from_list:
            self._list.selection_clear(0, "end")
            self._list.selection_set(index)
            self._list.see(index)
        self._editor.pack(fill="x", padx=6)
        self._load_editor()

    def _load_editor(self) -> None:
        """The selected mapping, into the form. Traces are guarded: writing
        a variable fires its commit, and a load that commits half-loaded
        fields would cross-copy mappings."""
        mapping = self._selected
        if mapping is None:
            return
        self._writing = True
        try:
            self._label_var.set(mapping.label)
            self._enabled_var.set(mapping.enabled)
            self._kind_var.set(mapping.kind)
            self._number_var.set(str(mapping.number))
            self._channel_var.set(str(mapping.channel))
            self._mode_var.set(mapping.mode)
            self._colour_var.set(str(mapping.colour))
            self._page_var.set(mapping.page)
            self._rebuild_actions()
        finally:
            self._writing = False

    # -- editing -----------------------------------------------------------

    def _commit(self) -> None:
        """The form, back into the mapping. Called on any change; cheap, and
        idempotent, so over-calling costs nothing."""
        mapping = self._selected
        if mapping is None or self._writing:
            return

        mapping.label = self._label_var.get().strip() or "mapping"
        mapping.enabled = bool(self._enabled_var.get())
        mapping.kind = self._kind_var.get()
        mapping.number = self._int_of(self._number_var, mapping.number, 0, 127)
        mapping.channel = self._int_of(self._channel_var, mapping.channel, 0, 16)
        mapping.mode = self._mode_var.get()
        mapping.colour = self._int_of(self._colour_var, mapping.colour, 0, 127)
        mapping.page = self._page_var.get().strip()

        # The action blocks, back onto the row. Zipped rather than indexed
        # because a commit can arrive from a focus-out *while* the editor is
        # being rebuilt for another mapping - one field's worth of a stale
        # form written into a shorter list would otherwise be an IndexError
        # at the desk.
        for action, row in zip(mapping.actions, self._action_rows):
            for name, var in row.param_vars.items():
                action.params[name] = var.get()

        self._mark_dirty()
        self._refresh_list(keep_selection=True)

    # -- the action list ---------------------------------------------------

    def _on_action_kind(self, index: int) -> None:
        """Another kind chosen in block `index`: a fresh action, with that
        kind's defaults. The old parameters are dropped rather than carried,
        because they named fields the new action does not have."""
        mapping = self._selected
        if mapping is None or index >= len(mapping.actions):
            return
        self._commit()
        key = self._action_labels.get(self._action_rows[index].action_var.get())
        if key is None or key == mapping.actions[index].key:
            return
        spec = ACTIONS.get(key)
        defaults = {fld.name: fld.default for fld in spec.fields} if spec else {}
        mapping.actions[index] = Action(key=key, params=defaults)
        self._mark_dirty()
        self._load_editor()
        self._refresh_list(keep_selection=True)

    def _add_action(self) -> None:
        """Another thing this pad does. The whole point of the list: a cue is
        usually a scene on the visualiser and a state on the rig."""
        mapping = self._selected
        if mapping is None:
            self._say("midi map: nothing selected")
            return
        self._commit()
        # `state` rather than another of whatever is already there: a second
        # action is nearly always the other half of the desk, and a duplicate
        # of the first is the one thing it is never going to be.
        spec = ACTIONS.get("state")
        defaults = {fld.name: fld.default for fld in spec.fields} if spec else {}
        mapping.actions.append(Action(key="state", params=defaults))
        self._mark_dirty()
        self._load_editor()
        self._refresh_list(keep_selection=True)

    def _remove_action(self, index: int) -> None:
        mapping = self._selected
        if mapping is None or len(mapping.actions) <= 1:
            return
        self._commit()
        del mapping.actions[index]
        self._mark_dirty()
        self._load_editor()
        self._refresh_list(keep_selection=True)

    @staticmethod
    def _int_of(var: tk.StringVar, fallback: int, low: int, high: int) -> int:
        try:
            return max(low, min(high, int(var.get().strip())))
        except ValueError:
            return fallback

    def _add(self) -> None:
        self.mappings.mappings.append(Mapping(label=f"map {len(self.mappings.mappings) + 1}"))
        self._mark_dirty()
        self._refresh_list()
        self._select_index(len(self.mappings.mappings) - 1)

    def _duplicate(self) -> None:
        mapping = self._selected
        if mapping is None:
            return
        copy = Mapping.from_dict(mapping.to_dict())
        copy.label += " copy"
        self.mappings.mappings.append(copy)
        self._mark_dirty()
        self._refresh_list()
        self._select_index(len(self.mappings.mappings) - 1)

    def _delete(self) -> None:
        mapping = self._selected
        if mapping is None:
            return
        rows = self.mappings.mappings
        index = rows.index(mapping)
        rows.remove(mapping)
        self._mark_dirty()
        self._refresh_list()
        self._select_index(index if rows else None)

    def _refresh_list(self, keep_selection: bool = False) -> None:
        selection = self._list.curselection()
        self._list.delete(0, "end")
        for mapping in self.mappings.mappings:
            dot = "●" if mapping.enabled else "○"
            # Trigger *and* what it does: the label is whatever was typed,
            # the trigger is the thing that cannot be seen anywhere else at a
            # glance, and the action count is how a row that grew a second
            # half announces itself without opening the editor.
            self._list.insert(
                "end",
                f" {dot} {mapping.label}  {mapping.describe_trigger()}"
                f" · {mapping.describe_actions()}")
        if keep_selection and selection:
            self._list.selection_set(selection[0])

    # -- learn -------------------------------------------------------------

    def _toggle_learn(self) -> None:
        """The editor's button: rebind the mapping already selected."""
        if self._selected is None:
            self._say("midi learn: nothing selected")
            return
        if self._learning:
            self._stop_learn()
        else:
            self._begin_learn()

    def _add_and_learn(self) -> None:
        """The list's button: a new mapping, bound by hitting the pad.

        Which is the whole flow from an empty map - press this, hit the pad,
        then say what it should do - rather than adding a mapping and typing
        a note number read off a controller chart.
        """
        self._add()
        self._begin_learn()

    def _begin_learn(self) -> None:
        if self._finding:
            self._stop_find()
        self._learning = True
        self._learn_button.configure(bg=BUTTON_BG_ACTIVE, text="hit it")
        self._add_learn_button.configure(bg=BUTTON_BG_ACTIVE)
        # Said as well as shown: the button is one word in a folded panel, and
        # the thing to do next - touch the controller - happens off screen.
        self._say("midi learn: hit a pad or move a control")

    def _stop_learn(self) -> None:
        self._learning = False
        self._learn_button.configure(bg=BUTTON_BG, text="learn")
        self._add_learn_button.configure(bg=BUTTON_BG)

    def take_learn(self, event: MidiEvent) -> bool:
        """Offered every event before the dispatcher sees it. Takes exactly
        one - the pad you hit - and only its press: the release half a
        second later must not re-learn the same pad as a release binding."""
        if not self._learning or self._selected is None:
            return False
        folded = fold_kind(event.kind)
        if folded is None:
            return False
        if event.kind == "note_off" or (folded != "program" and event.data2 == 0):
            return True  # the up-stroke of the pad being learned; eat it
        mapping = self._selected
        mapping.kind = folded
        mapping.channel = event.channel
        mapping.number = event.data1
        self._stop_learn()
        self._mark_dirty()
        self._load_editor()
        self._refresh_list(keep_selection=True)
        self._say(f"learned {mapping.describe_trigger()}")
        return True

    # -- find --------------------------------------------------------------

    def _toggle_find(self) -> None:
        """Arm the next pad hit to select its mapping instead of firing it."""
        if self._finding:
            self._stop_find()
            return
        if self._learning:
            self._stop_learn()
        self._finding = True
        self._find_button.configure(bg=BUTTON_BG_ACTIVE, text="hit it")
        # Said as well as shown, for the same reason learn says it: the thing
        # to do next happens off screen, on the controller.
        self._say("midi find: hit a pad to select what it does")

    def _stop_find(self) -> None:
        self._finding = False
        self._find_button.configure(bg=BUTTON_BG, text="find")

    def take_find(self, event: MidiEvent) -> bool:
        """Offered every event while armed, before the dispatcher sees it.

        Swallowed rather than passed on, both halves of the pad: asking what a
        button does should not also do it - on a show surface that could be
        the blackout - and the release would otherwise fire the binding that
        the press just selected.
        """
        if not self._finding:
            return False
        folded = fold_kind(event.kind)
        if folded is None:
            return False
        if event.kind == "note_off" or (folded != "program" and event.data2 == 0):
            return True  # the up-stroke of the pad being found; eat it

        rows = self.mappings.mappings
        matches = [index for index, mapping in enumerate(rows)
                   if mapping.matches(event)]
        self._stop_find()
        if not matches:
            self._say(f"midi find: nothing bound to {event.describe()}")
            return True

        # One pad with several bindings is normal - one per page - so the
        # selection steps: arm find again, hit the same pad, get the next one.
        # By identity rather than by value, because two rows that do the same
        # thing on different pages are equal as dataclasses.
        index = matches[0]
        current = next((i for i, mapping in enumerate(rows)
                        if mapping is self._selected), -1)
        if current in matches:
            index = matches[(matches.index(current) + 1) % len(matches)]

        self._select_index(index)
        mapping = rows[index]
        where = f" ({matches.index(index) + 1} of {len(matches)})" if len(matches) > 1 else ""
        self._say(f"found: {mapping.label} · {mapping.describe_actions()}{where}")
        return True

    # -- traffic, shown ----------------------------------------------------

    def show_traffic(self, event: Optional[MidiEvent], fired: List[str]) -> None:
        """Called once per pump with the newest event, not once per event: a
        VU mapping is hundreds of messages a second and a label repainted
        that often is where the frame budget goes to die."""
        if event is not None:
            self._heard.configure(text=f"in: {event.describe()}")
        if fired:
            self._did.configure(text=fired[-1][:44])

    # -- the file ----------------------------------------------------------

    def _mark_dirty(self) -> None:
        self._dirty = True
        self._title.configure(text="midi map *")

    def _mark_clean(self) -> None:
        self._dirty = False
        self._title.configure(text="midi map")

    def save(self) -> None:
        if self.path is None:
            self._save_as_dialog()
            return
        try:
            self.mappings.save(self.path)
        except OSError as error:
            self._say(f"midi map save: {error}")
            return
        self._mark_clean()
        self._say(f"midi map saved: {self.path.name}")

    def save_if_dirty(self) -> None:
        """On the way out: edits are worth more than the question. No path
        yet means the default file, so a first session's bindings survive."""
        if not self._dirty:
            return
        if self.path is None:
            self.path = self.default_dir / "default.json"
        try:
            self.mappings.save(self.path)
        except OSError:
            pass

    def _save_as_dialog(self) -> None:
        chosen = filedialog.asksaveasfilename(
            title="save midi mappings",
            initialdir=self._dialog_dir(),
            defaultextension=".json",
            filetypes=[("midi mappings", "*.json")])
        if not chosen:
            return
        self.path = Path(chosen)
        self.save()

    def _load_dialog(self) -> None:
        chosen = filedialog.askopenfilename(
            title="load midi mappings",
            initialdir=self._dialog_dir(),
            filetypes=[("midi mappings", "*.json")])
        if not chosen:
            return
        try:
            loaded = MappingSet.load(chosen)
        except (OSError, ValueError, KeyError) as error:
            self._say(f"midi map load: {error}")
            return
        # in place, because the dispatcher holds this same MappingSet
        self.mappings.mappings[:] = loaded.mappings
        self.path = Path(chosen)
        self._mark_clean()
        self._refresh_list()
        self._select_index(0 if self.mappings.mappings else None)
        self._say(f"midi map loaded: {self.path.name}")

    def _dialog_dir(self) -> str:
        try:
            self.default_dir.mkdir(parents=True, exist_ok=True)
        except OSError:
            pass
        return str(self.default_dir)
