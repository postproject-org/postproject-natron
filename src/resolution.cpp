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
  POSTPROJECT_TRY_ASSIGN(auto read, production.readSession());
  POSTPROJECT_TRY_ASSIGN(const auto base, read.decisionBase());
  POSTPROJECT_TRY_ASSIGN(const auto representation_id, binding.object.representationId());
  POSTPROJECT_TRY_ASSIGN(const auto representation,
                         read.representation(representation_id));
  if (!representation.imageSequence() || representation.resources.size() != 1)
    return Error(ErrorCode::invalid_argument,
                 "Binding is not a compact Reader sequence");
  const auto resource = representation.resources.front().id;
  // Inspect bounded locator pages for the Reader's alternative naming display.
  POSTPROJECT_TRY_ASSIGN(const auto locators,
                         read.locators(resource, 100));
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
      auto values, read.resolveAsset(representation.asset_id, options));
  if (std::any_of(values.begin(), values.end(), [&](const auto &value) {
        return value.representation_id == representation.id &&
               value.availability == RepresentationAvailability::online;
      })) {
    POSTPROJECT_TRY(options.setVerification(VerificationMode::content));
    POSTPROJECT_TRY_ASSIGN(
        values, read.resolveAsset(representation.asset_id, options));
  }
  for (const auto &value : values) {
    if (value.representation_id == representation.id &&
        value.resources.size() == 1)
      return Resolution{value.availability, value.resources.front().candidates(),
                        value.issues,       locators.items,
                        resource,           base};
  }
  return Error(ErrorCode::not_found,
               "Selected representation has no resolution result");
}

Result<void> confirm(const std::string &path, const std::string &binding_text,
                     const Resolution &resolution, std::size_t index) {
  if (resolution.availability != RepresentationAvailability::online ||
      resolution.candidates.size() != 1 || index != 0)
    return Error(ErrorCode::conflict,
                 "Incomplete or ambiguous sequence: native path unchanged");
  POSTPROJECT_TRY_ASSIGN(auto production, Production::open(path));
  POSTPROJECT_TRY_ASSIGN(const auto binding,
                         checked_binding(production, binding_text));
  POSTPROJECT_TRY_ASSIGN(auto read, production.readSession());
  POSTPROJECT_TRY_ASSIGN(const auto representation_id, binding.object.representationId());
  POSTPROJECT_TRY_ASSIGN(const auto representation,
                         read.representation(representation_id));
  if (representation.resources.size() != 1 ||
      representation.resources.front().id != resolution.resource)
    return Error(ErrorCode::conflict,
                 "Reader association changed; discard the result");
  const auto &candidate = resolution.candidates[index];
  // A verified locator already present is usable without staging it again.
  // Retain the decision's revision fence even for this read-only no-op.
  POSTPROJECT_TRY_ASSIGN(const auto locators,
                         read.locators(resolution.resource, 100));
  if (locators.next_cursor)
    return Error(ErrorCode::invalid_argument,
                 "Too many locators; refresh the Reader");
  if (std::any_of(
          locators.items.begin(), locators.items.end(), [&](const auto &item) {
            return item.locator.uri == candidate.uri &&
                   item.locator.sequence_naming == candidate.sequence_naming;
          })) {
    POSTPROJECT_TRY_ASSIGN(const auto current, read.decisionBase());
    if (current.production_id != resolution.base.production_id ||
        current.revision.has_value() != resolution.base.revision.has_value() ||
        (current.revision && current.revision->id != resolution.base.revision->id))
      return Error(ErrorCode::conflict,
                   "Production changed; discard this Reader decision");
    return {};
  }
  POSTPROJECT_TRY_ASSIGN(auto transaction,
                         production.edit(resolution.base));
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
  POSTPROJECT_TRY_ASSIGN(auto read, production.readSession());
  POSTPROJECT_TRY_ASSIGN(const auto representation_id, binding.object.representationId());
  POSTPROJECT_TRY_ASSIGN(const auto representation,
                         read.representation(representation_id));
  if (!representation.imageSequence() || representation.resources.size() != 1)
    return Error(ErrorCode::invalid_argument,
                 "Verification requires one sequence resource");
  return read.verifyResource(representation.resources.front().id,
                                   directory, naming);
}

Result<Refresh> refresh(const std::string &path,
                        const std::string &binding_text, std::uint64_t after) {
  POSTPROJECT_TRY_ASSIGN(auto production, Production::open(path));
  POSTPROJECT_TRY_ASSIGN(const auto binding,
                         checked_binding(production, binding_text));
  POSTPROJECT_TRY_ASSIGN(auto read, production.readSession());
  POSTPROJECT_TRY_ASSIGN(const auto representation_id, binding.object.representationId());
  POSTPROJECT_TRY_ASSIGN(const auto representation,
                         read.representation(representation_id));
  POSTPROJECT_TRY_ASSIGN(const auto asset,
                         read.asset(representation.asset_id));
  POSTPROJECT_TRY_ASSIGN(const auto revisions,
                         read.changesSince(after, 100));
  std::uint64_t through = after, events = 0;
  for (const auto &revision : revisions) {
    POSTPROJECT_TRY_ASSIGN(const auto page,
                           read.revisionEvents(revision.id, 1000));
    if (page.next_cursor)
      return Error(ErrorCode::unsupported,
                   "Revision has more than 1000 events; Reader refresh is bounded");
    events += page.items.size();
    through = revision.sequence;
  }
  return Refresh{through, events,
                 asset.display_name.value_or("Unnamed sequence")};
}
} // namespace pilot
