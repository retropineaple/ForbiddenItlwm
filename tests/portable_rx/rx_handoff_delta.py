"""Remove only the audited ADDBA challenger-recording delta for inherited RX guards."""
OLD='''                            if ((awdlCoexBaPeerAdded && !ItlAWDL::sameAddress(request.peer, awdlCoexBaPeer)) ||
                                !awdlCoexBaRequestsPending.note(request)) {
                                ++awdlCoexBaOtherPeer;'''
NEW='''                            if (awdlCoexBaPeerAdded && !ItlAWDL::sameAddress(request.peer, awdlCoexBaPeer)) {
                                ++awdlCoexBaOtherPeer;
                                if (awdlPeerHandoffEnabled())
                                    (void)awdlCoexPeerHandoff.note(request, awdlUptimeUS(), sc->sc_generation,
                                                                 awdlCoexToken, awdlCoexBaPeerEpoch);
                            } else if (!awdlCoexBaRequestsPending.note(request)) {
                                ++awdlCoexBaOtherPeer;'''
def without_handoff(text):
 assert text.count(NEW)==1
 return text.replace(NEW,OLD,1)
