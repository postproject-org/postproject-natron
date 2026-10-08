// SPDX-License-Identifier: GPL-2.0-or-later
// Installed C++ recipe; fixture bytes test identity, real Natron tests decode
// PNGs.
#include "adapter.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace pilot;

Result<void> require(bool condition, const char *message) {
  if (!condition)
    return Error(ErrorCode::internal, message);
  return {};
}

Result<void> exercise(const fs::path &root) {
  const auto original = root / "plates";
  fs::create_directories(original);
  for (const auto *prefix : {"plateA.", "plateB."}) {
    for (int frame = 1001; frame <= 1003; ++frame)
      std::ofstream(original / (prefix + std::to_string(frame) + ".png"))
          << prefix << frame;
  }
  const auto path = (root / "shared.pproj").string();
  POSTPROJECT_TRY_ASSIGN(auto production,
                         Production::create(path, "Native recipe"));
  POSTPROJECT_TRY_ASSIGN(const auto empty_base, production.latestRevision());
  POSTPROJECT_TRY(
      require(!empty_base, "New production unexpectedly has a base revision"));
  {
    POSTPROJECT_TRY_ASSIGN(auto abandoned, production.beginTransaction());
    POSTPROJECT_TRY(abandoned.rollback());
  }
  POSTPROJECT_TRY_ASSIGN(auto transaction, production.beginTransaction());
  ImageSequenceInput source{
      original.string(), {"plateA.", ".png", 4}, 1001, 1003, 1, 24, 1, {}};
  POSTPROJECT_TRY_ASSIGN(
      const auto asset,
      transaction.importMedia(MediaSource::imageSequence(source)));
  source.naming.prefix = "plateB.";
  POSTPROJECT_TRY(transaction.importMedia(MediaSource::imageSequence(source)));
  POSTPROJECT_TRY(transaction.commit());
  source.naming.prefix = "plateA.";
  POSTPROJECT_TRY_ASSIGN(const auto matches, candidates(path, source));
  POSTPROJECT_TRY(
      require(matches.size() == 1, "Naming-aware lookup mixed sequences"));
  POSTPROJECT_TRY_ASSIGN(
      const auto binding,
      associate(path, source, matches.front(), "contract-reader"));
  POSTPROJECT_TRY_ASSIGN(const auto assets, production.assets());
  POSTPROJECT_TRY(require(assets.size() == 2, "Adoption duplicated the asset"));
  POSTPROJECT_TRY_ASSIGN(const auto display, refresh(path, binding, 0));
  POSTPROJECT_TRY(require(display.through > 0 && display.events > 0,
                          "Refresh lost its durable cursor"));

  POSTPROJECT_TRY_ASSIGN(const auto already_here,
                         resolve(path, binding, original.string()));
  POSTPROJECT_TRY(confirm(path, binding, already_here, 0));
  POSTPROJECT_TRY_ASSIGN(const auto after_noop, production.latestRevision());
  POSTPROJECT_TRY(
      require(after_noop->id == already_here.base.revision->id,
              "Confirming a known locator manufactured a revision"));
  const auto moved = root / "moved";
  fs::rename(original, moved);
  POSTPROJECT_TRY_ASSIGN(const auto decision,
                         resolve(path, binding, moved.string()));
  POSTPROJECT_TRY(confirm(path, binding, decision, 0));
  POSTPROJECT_TRY_ASSIGN(const auto verified,
                         verify(path, binding, moved.string(), source.naming));
  POSTPROJECT_TRY(require(verified == ContentVerification::matches,
                          "Content identity changed during a directory move"));

  fs::rename(moved / "plateA.1002.png", root / "missing-frame");
  POSTPROJECT_TRY_ASSIGN(const auto partial,
                         resolve(path, binding, moved.string()));
  POSTPROJECT_TRY(
      require(partial.availability == RepresentationAvailability::partial,
              "Missing required frame did not produce partial availability"));
  POSTPROJECT_TRY(require(!confirm(path, binding, partial, 0),
                          "Partial sequence was confirmed"));
  fs::rename(root / "missing-frame", moved / "plateA.1002.png");

  const auto pending = root / "pending";
  fs::rename(moved, pending);
  POSTPROJECT_TRY_ASSIGN(const auto stale,
                         resolve(path, binding, pending.string()));
  const auto alternative = root / "other";
  fs::copy(pending, alternative, fs::copy_options::recursive);
  POSTPROJECT_TRY_ASSIGN(auto other, Production::open(path));
  POSTPROJECT_TRY_ASSIGN(auto change, other.beginTransaction());
  POSTPROJECT_TRY_ASSIGN(const auto uri, fileLocator(alternative.string()));
  POSTPROJECT_TRY(
      change.confirmLocator(stale.resource, uri, std::nullopt, source.naming));
  POSTPROJECT_TRY(change.commit());
  const auto conflicted = confirm(path, binding, stale, 0);
  POSTPROJECT_TRY(
      require(!conflicted && conflicted.error().transactionConflict(),
              "Stale decision lost structured conflict details"));
  // The failed commit is terminal; adapter destruction releases it. Re-read
  // rather than relabelling the old decision with the writer's latest revision.
  POSTPROJECT_TRY_ASSIGN(const auto current,
                         resolve(path, binding, alternative.string()));
  POSTPROJECT_TRY(require(current.base.revision->sequence > stale.base.revision->sequence,
                          "Conflict refresh did not advance the base"));
  POSTPROJECT_TRY_ASSIGN(const auto selected, production.asset(asset));
  POSTPROJECT_TRY(
      require(selected.id == asset, "Stable asset identity changed"));
  return {};
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  try {
    const fs::path root = argv[1];
    fs::remove_all(
        root); // This argument is the dedicated CTest fixture directory.
    const auto result = exercise(root);
    if (!result) {
      std::cerr << result.error().message() << '\n';
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
