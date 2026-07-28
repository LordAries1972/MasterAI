# MasterAI Initial UI Wireframes

## Setup-only mode

```text
+------------------------------------------------------+
| MasterAI Initial Setup                               |
| Setup token: [____________________________________]  |
| Administrator: [__________________________________]  |
| Password:      [__________________________________]  |
|                     [Create administrator]           |
+------------------------------------------------------+
```

## Model inventory

```text
+----------------------+-------------------------------+
| Models               | Selected model                |
| General programming  | State: Ready / Blocked        |
| Code completion      | RAM required / available      |
| Code review          | Backend and manifest status   |
| Debugging            | [Load] [Unload] [Details]     |
+----------------------+-------------------------------+
```

Errors must identify the failed control, the safe corrective action, and the next validation step. Security blocks must never be presented as successful or silently bypassed.
