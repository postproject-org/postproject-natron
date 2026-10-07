// SPDX-License-Identifier: GPL-2.0-or-later
#include "adapter.h"
#include <algorithm>

namespace pilot {
Result<HostObjectBinding> checked_binding(const Production &production,
                                          const std::string &value) {
  POSTPROJECT_TRY_ASSIGN(auto binding, HostObjectBinding::fromString(value));
  POSTPROJECT_TRY_ASSIGN(const auto production_id, production.id());
  if (binding.production_id != production_id ||
      binding.object.kind() != ObjectKind::representation)
    return Error(ErrorCode::invalid_argument,
                 "Binding names another production or object kind");
  return binding;
}

Result<std::vector<std::string>>
candidates(const std::string &path, const ImageSequenceInput &sequence) {
  POSTPROJECT_TRY_ASSIGN(auto production, Production::open(path));
  POSTPROJECT_TRY_ASSIGN(const auto production_id, production.id());
  POSTPROJECT_TRY_ASSIGN(const auto uri, fileLocator(sequence.directory));
  POSTPROJECT_TRY_ASSIGN(const auto page, production.findKnownMediaByLocator(
                                              {uri, sequence.naming}, 100));
  if (page.next_cursor)
    return Error(ErrorCode::invalid_argument,
                 "More than 100 matches; narrow the sequence locator");
  std::vector<std::string> result;
  for (const auto &match : page.items) {
    POSTPROJECT_TRY_ASSIGN(auto binding,
                           (HostObjectBinding{production_id,
                                              ObjectRef::representation(match.representation_id)})
                               .toString());
    result.push_back(std::move(binding));
  }
  return result;
}

Result<std::string> associate(const std::string &path,
                              const ImageSequenceInput &sequence,
                              const std::string &selected,
                              const std::string &reader_id) {
  POSTPROJECT_TRY_ASSIGN(auto production, Production::open(path));
  POSTPROJECT_TRY_ASSIGN(const auto production_id, production.id());
  POSTPROJECT_TRY_ASSIGN(const auto matches, candidates(path, sequence));
  std::optional<AssetId> asset;
  std::optional<RepresentationId> representation;
  if (!selected.empty()) {
    if (std::find(matches.begin(), matches.end(), selected) == matches.end())
      return Error(ErrorCode::conflict,
                   "Selected candidate is no longer at this locator; refresh");
    POSTPROJECT_TRY_ASSIGN(const auto binding,
                           checked_binding(production, selected));
    POSTPROJECT_TRY_ASSIGN(const auto representation_id, binding.object.representationId());
    POSTPROJECT_TRY_ASSIGN(const auto value,
                           production.representation(representation_id));
    if (!value.imageSequence())
      return Error(ErrorCode::invalid_argument,
                   "Reader requires an image sequence");
    // Frame domain and playback rate are separate from the directory naming.
    if (value.imageSequence()->start != sequence.start ||
        value.imageSequence()->end != sequence.end ||
        value.imageSequence()->step != sequence.step)
      return Error(ErrorCode::conflict,
                   "Reader frame domain differs from the selected sequence");
    asset = value.asset_id;
    representation = value.id;
  } else if (!matches.empty()) {
    return Error(ErrorCode::conflict,
                 "Choose an existing candidate explicitly");
  }
  POSTPROJECT_TRY_ASSIGN(auto transaction, production.beginTransaction());
  POSTPROJECT_TRY(transaction.setRevisionContext(
      {OriginIdentity{"fr.inria.Natron", "2.5.0", std::nullopt},
       "Associate Reader sequence"}));
  if (!asset) {
    POSTPROJECT_TRY_ASSIGN(
        asset, transaction.importMedia(MediaSource::imageSequence(sequence),
                                       "Natron sequence"));
  }
  POSTPROJECT_TRY(transaction.addExternalIdentifier(
      ObjectRef::asset(*asset),
      {"fr.inria.Natron:reader", reader_id, "image-sequence"}));
  POSTPROJECT_TRY(transaction.commit());
  if (!representation) {
    POSTPROJECT_TRY_ASSIGN(const auto values,
                           production.representations(*asset));
    if (values.size() != 1)
      return Error(ErrorCode::conflict,
                   "Imported asset has an unexpected representation set");
    representation = values.front().id;
  }
  return HostObjectBinding{production_id,
                           ObjectRef::representation(*representation)}
      .toString();
}
} // namespace pilot
