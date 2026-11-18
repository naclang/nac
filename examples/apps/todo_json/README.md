# NaC Todo (JSON file)

A small web-based todo app with an HTML interface and a JSON read endpoint.
Tasks are stored in `examples/apps/todo_json/todos.json` and survive server restarts.

From the repository root, run:

```text
nac examples/apps/todo_json/todo.nac
```

Open `http://localhost:8080`. Add tasks, mark them complete, or delete them.
The current list is also available at `GET /api/todos`.
Tasks are stored in `examples/apps/todo_json/todos.json`. Set `TODO_FILE` to use a
different JSON file.
