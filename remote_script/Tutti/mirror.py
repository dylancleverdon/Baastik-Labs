"""This computer's copy of the shared document (confirmed edits plus our own pending ones).

Same model as core/include/tutti/Doc.h: entities {id: {"t": type, "p": parent, "v": props}}."""


class Mirror(object):
    ROOT = "song"

    def __init__(self):
        self.reset()

    def reset(self):
        self.ent = {self.ROOT: {"t": "song", "p": "", "v": {}}}
        self.kids = {}

    def load(self, entities):
        """entities: list of {"id","t","p","v"} with parents first."""
        self.reset()
        for e in entities:
            if e["id"] == self.ROOT:
                self.ent[self.ROOT]["v"] = dict(e.get("v") or {})
            else:
                self.create(e["id"], e["t"], e["p"], e.get("v") or {})

    def to_json(self):
        return {"e": [self.entity_json(i) for i in self.subtree(self.ROOT)]}

    def has(self, eid):
        return eid in self.ent

    def get(self, eid):
        return self.ent.get(eid)

    def props(self, eid):
        e = self.ent.get(eid)
        return e["v"] if e else None

    def type_of(self, eid):
        e = self.ent.get(eid)
        return e["t"] if e else None

    def parent(self, eid):
        e = self.ent.get(eid)
        return e["p"] if e else None

    def create(self, eid, etype, parent, props):
        if eid in self.ent:
            self.set(eid, props)
            return True
        if parent not in self.ent:
            return False
        self.ent[eid] = {"t": etype, "p": parent, "v": {k: v for k, v in (props or {}).items() if v is not None}}
        self.kids.setdefault(parent, []).append(eid)
        return True

    def set(self, eid, props):
        e = self.ent.get(eid)
        if e is None:
            return False
        for k, v in (props or {}).items():
            if v is None:
                e["v"].pop(k, None)
            else:
                e["v"][k] = v
        return True

    def replace(self, eid, props):
        e = self.ent.get(eid)
        if e is not None:
            e["v"] = dict(props or {})

    def remove(self, eid):
        if eid == self.ROOT or eid not in self.ent:
            return []
        removed = self.subtree(eid)
        parent = self.ent[eid]["p"]
        for r in removed:
            self.ent.pop(r, None)
            self.kids.pop(r, None)
        siblings = self.kids.get(parent)
        if siblings and eid in siblings:
            siblings.remove(eid)
        return removed

    def children(self, eid, etype=None):
        out = []
        for c in self.kids.get(eid, ()):
            if etype is None or self.ent[c]["t"] == etype:
                out.append(c)
        return out

    def ordered(self, eid, etype, where=None):
        items = self.children(eid, etype)
        if where is not None:
            items = [c for c in items if where(self.ent[c]["v"])]
        return sorted(items, key=lambda c: (float(self.ent[c]["v"].get("pos", 0.0)), c))

    def subtree(self, eid):
        if eid not in self.ent:
            return []
        out = []
        stack = [eid]
        while stack:
            cur = stack.pop()
            out.append(cur)
            stack.extend(reversed(self.kids.get(cur, ())))
        return out

    def ancestors(self, eid):
        out = []
        e = self.ent.get(eid)
        while e is not None and e["p"]:
            out.append(e["p"])
            e = self.ent.get(e["p"])
        return out

    def entity_json(self, eid):
        e = self.ent[eid]
        return {"id": eid, "t": e["t"], "p": e["p"], "v": dict(e["v"])}

    def apply_op(self, op):
        kind = op.get("k")
        if kind == "c":
            return self.create(op["id"], op["t"], op["p"], op.get("v") or {})
        if kind == "r":
            self.remove(op["id"])
            return True
        if kind == "s":
            return self.set(op["id"], op.get("v") or {})
        return False
