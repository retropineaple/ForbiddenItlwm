// Detect outbound payload bursts within a quarter second after BA is ready.
    (void)ItlAWDL::noteBaTxTraffic(awdlCoexBaTxBulk, now, awdlCoexTxDestFrames,
                                  awdlCoexBaTxState == kCoexBaTxAgreed);

    if (!awdlCoexBaPeerAdded) {
        if (!awdlCoexBaTxBulk.bulk || !ItlAWDL::knownAddress(awdlCoexTxDest) ||
            !awdlCoexPeerKnown(awdlCoexTxDest, now))
            return;
        ++awdlCoexBaTxPeerAdds;
        if (awdlCoexBaAddPeer(sc, awdlCoexTxDest))
            return;
        if (!stillOurs()) {
            (void)awdlCoexBaRemovePeer(sc);
            return;
        }
    }
    // Dedicated mode negotiates as soon as the peer ACKs our receive BA answer.
    // Keep the bulk trigger for peers that have not opened an inbound session.
    if (!awdlCoexBaPeerAccepted &&
        (!awdlCoexBaTxBulk.bulk || !ItlAWDL::sameAddress(awdlCoexTxDest, awdlCoexBaPeer)))
        return;
    if (awdlCoexBaTxState == kCoexBaTxOff)
