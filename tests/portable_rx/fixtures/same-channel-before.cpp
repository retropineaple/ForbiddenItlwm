awdlCoexMacSend(struct iwx_softc *sc, uint32_t action)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct iwx_node *in = (struct iwx_node *)ic->ic_bss;
    struct iwx_mac_ctx_cmd mac;
    memset(&mac, 0, sizeof(mac));
    mac.id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(kAwdlCoexMacId, 0));
    mac.action = htole32(action);
    bool promiscWanted = false;
    if (action != IWX_FW_CTXT_ACTION_REMOVE) {
        mac.mac_type = htole32(IWX_FW_MAC_TYPE_LISTENER);
        mac.tsf_id = htole32(IWX_TSF_ID_B);
        if (awdlCoexAddrKnown) {
            IEEE80211_ADDR_COPY(mac.node_addr, awdlCoexAddr);
        } else {
            IEEE80211_ADDR_COPY(mac.node_addr, ic->ic_myaddr);
            mac.node_addr[0] |= 0x02;
            mac.node_addr[5] ^= 0x5a;
        }
        static const uint8_t awdlBssid[6] = {0x00, 0x25, 0x00, 0xff, 0x94, 0x73};
        IEEE80211_ADDR_COPY(mac.bssid_addr, (awdlCoexFilter == 1 || awdlCoexFilter == 3) ? awdlBssid : etherbroadcastaddr);
        int cck = 0, ofdm = 0;
        if (awdlCoexOwnChan) {
            // Its own channel's band, not the BSS's: the mandatory OFDM rates (6, 12, 24 Mbit/s) on both
            // bands, CCK only on 2.4 GHz.
            cck = IEEE80211_IS_CHAN_2GHZ(awdlCoexOwnChan) ? 0x0f : 0;
            ofdm = 0x15;
        } else {
            iwx_ack_rates(sc, in, &cck, &ofdm);
        }
        mac.cck_rates = htole32(cck);
        mac.ofdm_rates = htole32(ofdm);
        // Filter 3 (the default with -itlawdlcoexdata): AWDL's BSSID and group frames, plus promiscuous
        // receive while the listener is on its own channel, different from the station's. A LISTENER
        // context without it passes AWDL's broadcast action frames but no data (iface130: AwdlData 0 on
        // both channels), and dedicated mode, where data arrives, is always promiscuous. On the station's
        // own channel it would take the station's frames from it (iface108), so not there.
        struct ieee80211_channel *stationChan = in && in->in_phyctxt ? in->in_phyctxt->channel : nullptr;
        promiscWanted = awdlCoexFilter == 3 && !awdlCoexBaFilterOff && awdlCoexOwnChan && stationChan &&
            ieee80211_chan2ieee(ic, awdlCoexOwnChan) != ieee80211_chan2ieee(ic, stationChan);
        mac.filter_flags = htole32(awdlCoexFilter == 0 ?
            IWX_MAC_FILTER_IN_PROMISC | IWX_MAC_FILTER_IN_CONTROL_AND_MGMT | IWX_MAC_FILTER_ACCEPT_GRP |
            IWX_MAC_FILTER_IN_BEACON | IWX_MAC_FILTER_IN_PROBE_REQUEST :
            promiscWanted ? IWX_MAC_FILTER_IN_PROMISC | IWX_MAC_FILTER_IN_CONTROL_AND_MGMT |
                IWX_MAC_FILTER_ACCEPT_GRP :
            IWX_MAC_FILTER_ACCEPT_GRP);
    }
    // The state is recorded only once the firmware has taken the command. A failure leaves it unknown, and
    // the RX isolation then treats the listener as promiscuous until a later command succeeds.
    // An answer that comes back after the listener changed belongs to no one: isolation stays on
    // (uncertain) until a current command settles it. A firmware restart clears everything anyway.
    const uint32_t macTok = awdlCoexToken;
    const int macGen = sc->sc_generation;
    const int result = iwx_send_cmd_pdu(sc, IWX_MAC_CONTEXT_CMD, 0, sizeof(mac), &mac);
    if (macGen != sc->sc_generation)
        return result;
    if (!result && macTok == awdlCoexToken) {
        awdlCoexPromiscLive = promiscWanted;
        awdlCoexPromiscUncertain = false;
    } else {
        awdlCoexPromiscUncertain = true;
    }
    return result;
}

// The listener
