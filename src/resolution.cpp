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
  postproject::ResolutionConfig config;
  if (root.empty())
    config.search_directories = {directory};
  else
    config.root_mappings = {{root, directory}};
  POSTPROJECT_TRY_ASSIGN(auto options, ResolutionOptions::create(config));
  // Presence reports required-frame gaps. Verify content only for a complete
  // selected sequence; incomplete collection hashes cannot certify identity.
  POSTPROJECT_TRY_ASSIGN(
      auto values, read.resolveAsset(representation.asset_id, options));
  if (std::any_of(values.begin(), values.end(), [&](const auto &value) {
        return value.representation_id == representation.id &&
               value.availability == RepresentationAvailability::online;
      })) {
    config.verification = VerificationMode::content;
    POSTPROJECT_TRY_ASSIGN(options, ResolutionOptions::create(config));
    POSTPROJECT_TRY_ASSIGN(
        values, read.resolveAsset(representation.asset_id, options));
  }
  for (const auto &value : values) {
    if (value.representation_id == representation.id && value.resources.size() == 1) {
      const auto &resolved = value.resources.front();
      std::optional<RelinkSelection> selection;
      if (auto verified = value.selectVerified()) {
        selection = *std::move(verified);
      } else if (value.availability == RepresentationAvailability::online &&
                 resolved.candidates().size() == 1) {
        const auto &candidate = resolved.candidates().front();
        const auto incomplete = [](const auto &evidence) {
          return evidence.kind == EvidenceKind::search_truncated ||
                 evidence.kind == EvidenceKind::discovery_error;
        };
        // Reader sequence policy explicitly accepts recorded sampled collection
        // evidence after complete required-frame coverage; this is not a full hash.
        const bool sampled = std::any_of(candidate.evidence.begin(), candidate.evidence.end(),
            [](const auto &evidence) { return evidence.kind == EvidenceKind::partial_fingerprint_match; });
        if (sampled && !std::any_of(resolved.evidence.begin(), resolved.evidence.end(), incomplete) &&
            !std::any_of(candidate.evidence.begin(), candidate.evidence.end(), incomplete))
          selection = RelinkSelection{base, representation.id, {{resource, candidate}}};
      }
      return Resolution{value.availability, resolved.candidates(), value.issues,
                        locators.items, resource, base, std::move(selection)};
    }
  }
  return Error(ErrorCode::not_found,
               "Selected representation has no resolution result");
}

Result<void> confirm(const std::string &path, const std::string &binding_text,
                     const Resolution &resolution, std::size_t index) {
  if (resolution.availability != RepresentationAvailability::online ||
      resolution.candidates.size() != 1 || index != 0 || !resolution.selection)
    return Error(ErrorCode::conflict,
                 "Incomplete or ambiguous sequence: native path unchanged");
  POSTPROJECT_TRY_ASSIGN(auto production, Production::open(path));
  POSTPROJECT_TRY_ASSIGN(const auto binding,
                         checked_binding(production, binding_text));
  POSTPROJECT_TRY_ASSIGN(auto read, production.readSession());
  POSTPROJECT_TRY_ASSIGN(const auto representation_id, binding.object.representationId());
  POSTPROJECT_TRY_ASSIGN(const auto representation,
                         read.representation(representation_id));
  if (representation.id != resolution.selection->representation_id ||
      representation.resources.size() != 1 ||
      representation.resources.front().id != resolution.resource)
    return Error(ErrorCode::conflict,
                 "Reader association changed; discard the result");
  POSTPROJECT_TRY_ASSIGN(auto transaction, production.edit(resolution.base,
      {OriginIdentity{"fr.inria.Natron", "2.5.0", std::nullopt},
       "Confirm Reader locator"}));
  POSTPROJECT_TRY(transaction.confirmSelection(*resolution.selection));
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
