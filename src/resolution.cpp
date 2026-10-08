// SPDX-License-Identifier: GPL-2.0-or-later
#include "adapter.h"
#include <algorithm>

namespace pilot {
Result<Resolution> resolve(const std::string &path,
                           const std::string &binding_text,
                           const std::string &directory,
                           const std::string &root) {
  POSTPROJECT_TRY_ASSIGN(auto production, Production::open(path));
  POSTPROJECT_TRY_ASSIGN(const auto binding,
                         checked_binding(production, binding_text));
  // Take the decision base before reading. A later revision is never our commit
  // receipt.
  POSTPROJECT_TRY_ASSIGN(const auto before, production.latestRevision());
  POSTPROJECT_TRY_ASSIGN(const auto representation_id, binding.object.representationId());
  POSTPROJECT_TRY_ASSIGN(const auto representation,
                         production.representation(representation_id));
  if (!representation.imageSequence() || representation.resources.size() != 1)
    return Error(ErrorCode::invalid_argument,
                 "Binding is not a compact Reader sequence");
  const auto resource = representation.resources.front().id;
  // Inspect bounded locator pages for the Reader's alternative naming display.
  POSTPROJECT_TRY_ASSIGN(const auto locators,
                         production.locators(resource, 100));
  if (locators.next_cursor)
    return Error(ErrorCode::invalid_argument,
                 "Too many locators for the selected Reader");
  POSTPROJECT_TRY_ASSIGN(auto options, ResolutionOptions::create());
  if (root.empty())
    POSTPROJECT_TRY(options.addSearchDirectory(directory));
  else
    POSTPROJECT_TRY(options.addRootMapping(root, directory));
  // Presence reports required-frame gaps. Verify content only for a complete
  // selected sequence; incomplete collection hashes cannot certify identity.
  POSTPROJECT_TRY_ASSIGN(
      auto values, production.resolveAsset(representation.asset_id, options));
  if (std::any_of(values.begin(), values.end(), [&](const auto &value) {
        return value.representation_id == representation.id &&
               value.availability == RepresentationAvailability::online;
      })) {
    POSTPROJECT_TRY(options.setVerification(VerificationMode::content));
    POSTPROJECT_TRY_ASSIGN(
        values, production.resolveAsset(representation.asset_id, options));
  }
  POSTPROJECT_TRY_ASSIGN(const auto after, production.latestRevision());
  if (before.has_value() != after.has_value() ||
      (before && before->id != after->id))
    return Error(
        ErrorCode::conflict,
        "Production changed during resolution; refresh before deciding");
  for (const auto &value : values) {
    if (value.representation_id == representation.id &&
        value.resources.size() == 1)
      return Resolution{value.availability, value.resources.front().candidates(),
                        value.issues,       locators.items,
                        resource,           before};
  }
  return Error(ErrorCode::not_found,
               "Selected representation has no resolution result");
}

Result<void> confirm(const std::string &path, const std::string &binding_text,
                     const Resolution &resolution, std::size_t index) {
  if (resolution.availability != RepresentationAvailability::online ||
      resolution.candidates.size() != 1 || index != 0 || !resolution.base)
    return Error(ErrorCode::conflict,
                 "Incomplete or ambiguous sequence: native path unchanged");
  POSTPROJECT_TRY_ASSIGN(auto production, Production::open(path));
  POSTPROJECT_TRY_ASSIGN(const auto binding,
                         checked_binding(production, binding_text));
  POSTPROJECT_TRY_ASSIGN(const auto representation_id, binding.object.representationId());
  POSTPROJECT_TRY_ASSIGN(const auto representation,
                         production.representation(representation_id));
  if (representation.resources.size() != 1 ||
      representation.resources.front().id != resolution.resource)
    return Error(ErrorCode::conflict,
                 "Reader association changed; discard the result");
  const auto &candidate = resolution.candidates[index];
  // A verified locator already present is usable without staging it again.
  // Retain the decision's revision fence even for this read-only no-op.
  POSTPROJECT_TRY_ASSIGN(const auto locators,
                         production.locators(resolution.resource, 100));
  if (locators.next_cursor)
    return Error(ErrorCode::invalid_argument,
                 "Too many locators; refresh the Reader");
  if (std::any_of(
          locators.items.begin(), locators.items.end(), [&](const auto &item) {
            return item.locator.uri == candidate.uri &&
                   item.locator.sequence_naming == candidate.sequence_naming;
          })) {
    POSTPROJECT_TRY_ASSIGN(const auto latest, production.latestRevision());
    if (!latest || latest->id != resolution.base->id)
      return Error(ErrorCode::conflict,
                   "Production changed; discard this Reader decision");
    return {};
  }
  POSTPROJECT_TRY_ASSIGN(auto transaction,
                         production.beginTransaction(resolution.base->id));
  POSTPROJECT_TRY(transaction.setRevisionContext(
      {OriginIdentity{"fr.inria.Natron", "2.5.0", std::nullopt},
       "Confirm Reader locator"}));
  POSTPROJECT_TRY(transaction.confirmLocator(resolution.resource, candidate.uri,
                                             candidate.media_root,
                                             candidate.sequence_naming));
  POSTPROJECT_TRY(transaction.commit());
  return {};
}

Result<ContentVerification> verify(const std::string &path,
                                   const std::string &binding_text,
                                   const std::string &directory,
                                   const SequenceNaming &naming) {
  POSTPROJECT_TRY_ASSIGN(auto production, Production::open(path));
  POSTPROJECT_TRY_ASSIGN(const auto binding,
                         checked_binding(production, binding_text));
  POSTPROJECT_TRY_ASSIGN(const auto representation_id, binding.object.representationId());
  POSTPROJECT_TRY_ASSIGN(const auto representation,
                         production.representation(representation_id));
  if (!representation.imageSequence() || representation.resources.size() != 1)
    return Error(ErrorCode::invalid_argument,
                 "Verification requires one sequence resource");
  return production.verifyResource(representation.resources.front().id,
                                   directory, naming);
}

Result<Refresh> refresh(const std::string &path,
                        const std::string &binding_text, std::uint64_t after) {
  POSTPROJECT_TRY_ASSIGN(auto production, Production::open(path));
  POSTPROJECT_TRY_ASSIGN(const auto binding,
                         checked_binding(production, binding_text));
  POSTPROJECT_TRY_ASSIGN(const auto representation_id, binding.object.representationId());
  POSTPROJECT_TRY_ASSIGN(const auto representation,
                         production.representation(representation_id));
  POSTPROJECT_TRY_ASSIGN(const auto asset,
                         production.asset(representation.asset_id));
  POSTPROJECT_TRY_ASSIGN(const auto revisions,
                         production.changesSince(after, 100));
  std::uint64_t through = after, events = 0;
  for (const auto &revision : revisions) {
    POSTPROJECT_TRY_ASSIGN(const auto page,
                           production.revisionEvents(revision.id));
    events += page.size();
    through = revision.sequence;
  }
  return Refresh{through, events,
                 asset.display_name.value_or("Unnamed sequence")};
}
} // namespace pilot
