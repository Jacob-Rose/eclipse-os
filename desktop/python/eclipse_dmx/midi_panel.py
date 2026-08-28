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

        def option(parent, var, choices) -> tk.OptionMenu:
            widget = tk.OptionMenu(parent, var, *choices,
                                   command=lambda _v: self._commit())
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

        # the action, and the form it asks for
        row = tk.Frame(self._editor, bg=PANEL)
        row.pack(fill="x", pady=(6, 1))
        dim_label(row, "do    ").pack(side="left")
        self._action_labels = {spec.label: key for key, spec in ACTIONS.items()}
        self._action_var = tk.StringVar()
        option(row, self._action_var, list(self._action_labels)).pack(side="left")

        self._params_frame = tk.Frame(self._editor, bg=PANEL)
        self._params_frame.pack(fill="x", pady=(2, 4))
        self._param_vars: Dict[str, tk.StringVar] = {}
        self._params_built_for: Optional[str] = None

        self._entry_factory = entry
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

    # -- the params form, drawn from the spec ------------------------------

    def _rebuild_params(self, action_key: str) -> None:
        """One row per field the action declares. This is what makes a new
        action registered in midi_map editable here with no edit here."""
        for child in self._params_frame.winfo_children():
            child.destroy()
        self._param_vars = {}
        self._params_built_for = action_key

        spec = ACTIONS.get(action_key)
        if spec is None:
            return
        for fld in spec.fields:
            row = tk.Frame(self._params_frame, bg=PANEL)
            row.pack(fill="x", pady=1)
            self._dim_label(row, f"  {fld.label:<7}").pack(side="left")
            var = tk.StringVar()
            self._param_vars[fld.name] = var
            width = 6 if fld.kind in ("int", "float") else 22
            self._entry_factory(row, var, width=width).pack(side="left")

    # -- selection ---------------------------------------------------------

    def _on_select(self) -> None:
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
            spec = ACTIONS.get(mapping.action)
            self._action_var.set(spec.label if spec else mapping.action)
            self._rebuild_params(mapping.action)
            for name, var in self._param_vars.items():
                var.set(str(mapping.params.get(name, "")))
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

        action_key = self._action_labels.get(self._action_var.get(), mapping.action)
        if action_key != mapping.action:
            mapping.action = action_key
            mapping.params = {}
            self._rebuild_params(action_key)
            spec = ACTIONS.get(action_key)
            if spec is not None:
                self._writing = True
                try:
                    for fld in spec.fields:
                        self._param_vars[fld.name].set(str(fld.default))
                finally:
                    self._writing = False
        for name, var in self._param_vars.items():
            mapping.params[name] = var.get()

        self._mark_dirty()
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
            self._list.insert(
                "end", f" {dot} {mapping.label}  ({mapping.describe_trigger()})")
        if keep_selection and selection:
            self._list.selection_set(selection[0])

    # -- learn -------------------------------------------------------------

    def _toggle_learn(self) -> None:
        if self._selected is None:
            self._say("midi learn: nothing selected")
            return
        self._learning = not self._learning
        self._learn_button.configure(
            bg=BUTTON_BG_ACTIVE if self._learning else BUTTON_BG,
            text="hit it" if self._learning else "learn")

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
        self._learning = False
        self._learn_button.configure(bg=BUTTON_BG, text="learn")
        self._mark_dirty()
        self._load_editor()
        self._refresh_list(keep_selection=True)
        self._say(f"learned {mapping.describe_trigger()}")
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
