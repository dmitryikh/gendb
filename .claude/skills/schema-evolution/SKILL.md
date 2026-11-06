---
name: schema-evolution
description: Keeps gendb schemas growing in ways that existing data can still be read. Use this whenever a `.gendb` schema file or a database YAML file (`collections`, `sequences`, `indices`) is about to change or is being reviewed, including when someone asks to add, remove, rename or retype a field, change a default, change a primary key, reorder or renumber anything, or "clean up" a schema, even if they do not mention compatibility or evolution. Also use it to explain why the schema check in CI failed.
---

# Schema evolution

A schema describes data that may already exist: in stored records, in the log, and on replicas running another version of the program. A record holds field numbers, not names, and a reader uses the schema it was compiled with, so a change that looks harmless in the file can silently change what old bytes mean. Hence the rule: **schemas only grow**.

`docs/schema.md` is the reference for the language. The parser already rejects gaps and reordering, so what this skill adds is what the parser cannot see: what the file used to say.

## When a change is requested

Work out which row below it falls under, and make the allowed form of it. People asking to rename or remove a field usually want the outcome, not that particular edit, so offer the form that gets them there.

| They want to | What to do | Why |
| --- | --- | --- |
| Add a field | Add it at the end, with the next number | Old records simply lack it, and old readers keep it as an unknown field |
| Stop using a field | Mark it `deprecated` where it is, and remove its uses in code | Its number must never mean anything else, and stored values survive rewrites |
| Rename a field | Deprecate it, and add a field with the new name | Generated names are API, and a rename reads as a different field in any diff |
| Change a field's type, even to a wider one | Deprecate it, add a new field, and migrate in application code if old values matter | A different storage class reads as absent, so the data would seem to vanish |
| Change or add a default on an existing field | Deprecate it, and add a new field with that default | A default is not stored: changing it changes the meaning of every record that does not set the field |
| Add an enumerator | Add it at the end, with the next number | Readers that do not know it still get the number |
| Remove or rename an enumerator | Keep it, and stop writing it | Records may hold its number |
| Change a primary key | Add a new collection, and migrate in application code | The key is how stored records are found |
| Change a collection's type | Add a new collection | The stored bytes are of the old type |
| Remove a collection | Allowed, with a warning | Its data is kept out of reach, and using `discard_unknown_collections` to drop it is a separate decision for the user |
| Add a sequence | Add it at the end of the list | A sequence is stored by its place in the list, so inserting or removing one would give another's counter |
| Change indices | Free, as each side builds its own. A newly `unique` index gets a warning | Existing records are not checked against it, so duplicates could already be there |

Fixing a check failure by changing numbers to make them line up would hide the problem rather than solve it: if the check or the parser complains, the change itself needs another form.

## Things to think about while adding a field

- **Set or default?** A default is what a getter returns when the field is not set. `has_x()` stays false, so a record relying on a default is not found through an index on that field, and it does not count as having a primary key. Say so if the code being written seems to expect otherwise.
- **Choose defaults carefully.** They cannot change later, so a value that may need to be tuned belongs in configuration, not in a schema.
- **Who reads it?** With a rolling upgrade, old readers see the new field as unknown, and they keep it through updates.

## After editing

1. Check the change against what the branch started from. The check needs `python3` with `PyYAML`, which the project's Docker image has, and the git history:

   ```bash
   docker run --rm -v "$PWD":/workspace -w /workspace gendb-dev python3 gen/schema_evolution.py --base origin/main
   ```

2. Regenerate and build, as the generator and the compiler check the rest. `scripts/ci/run.sh test`, run the same way, does it, and regenerated files under `tests/generated` and `lib/gendb/generated` belong in the commit.

3. Tell the user what the check said. Pass on any warnings, as each is a decision about data. Errors mean the change would break existing data, so explain which row above applies, rather than working around the check.

CI runs the same check on every pull request, as the `schema` job.

## When someone asks for a breaking change anyway

The rules are the project's by design, so do not relax the check or edit it to get a change through. If the user says that no data exists yet that this could break, and wants the change regardless, that is their call: make it, and say plainly that the `schema` job will fail on that pull request, and why.
