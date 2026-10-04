#pragma once
#include <postproject/postproject.hpp>

namespace pilot {
using namespace postproject;

struct Resolution {
  RepresentationAvailability availability;
  std::vector<ResolutionCandidate> candidates;
  std::vector<AvailabilityIssue> issues;
  std::vector<ResourceLocator> locators;
  Uuid resource;
  std::optional<Revision> base;
};
struct Refresh {
  std::uint64_t through;
  std::uint64_t events;
  std::string asset_name;
};

Result<std::vector<std::string>> candidates(const std::string &production,
                                            const ImageSequenceInput &sequence);
Result<std::string> associate(const std::string &production,
                              const ImageSequenceInput &sequence,
                              const std::string &selected,
                              const std::string &reader_id);
Result<Resolution> resolve(const std::string &production,
                           const std::string &binding,
                           const std::string &directory,
                           const std::string &root = "");
Result<void> confirm(const std::string &production, const std::string &binding,
                     const Resolution &resolution, std::size_t candidate);
Result<ContentVerification> verify(const std::string &production,
                                   const std::string &binding,
                                   const std::string &directory,
                                   const SequenceNaming &naming);
Result<Refresh> refresh(const std::string &production,
                        const std::string &binding, std::uint64_t after);
Result<HostObjectBinding> checked_binding(const Production &production,
                                          const std::string &binding);
} // namespace pilot
