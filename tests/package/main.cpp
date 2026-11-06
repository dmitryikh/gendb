#include <cstdio>
#include <filesystem>

#include "database.h"
#include "note.gendb.h"

int main() {
  const auto path = std::filesystem::temp_directory_path() / "gendb_package_test";
  std::filesystem::remove_all(path);
  {
    auto db = notes::Db::Open({.path = path.string()});
    if (!db.ok()) return 1;
    auto writer = (*db)->CreateWriter();
    if (!writer.PutNote(notes::NoteBuilder().set_id(1).set_title("hello").set_stars(5).Build())
             .ok()) {
      return 2;
    }
    if (!writer.Commit().ok()) return 3;
  }
  auto db = notes::Db::Open({.path = path.string()});
  if (!db.ok()) return 4;
  auto snapshot = (*db)->CreateSnapshot();
  auto it = snapshot.GetNoteByStarsEqual(5);
  if (!it.Valid() || it.Value().title() != "hello") return 5;
  std::printf("The installed package works: %s\n", std::string(it.Value().title()).c_str());
  db->reset();
  std::filesystem::remove_all(path);
  return 0;
}
