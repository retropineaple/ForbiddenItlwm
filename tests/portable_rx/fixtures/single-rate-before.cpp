iwx_tx_fill_cmd(struct iwx_softc *sc, struct iwx_node *in,
                struct ieee80211_frame *wh, uint32_t *flags, uint32_t *rate_n_flags)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct ieee80211_node *ni = &in->in_ni;
    struct ieee80211_rateset *rs = &ni->ni_rates;
    const struct iwx_rate *rinfo;
    int type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
    int min_ridx = iwx_rval2ridx(ieee80211_min_basic_rate(ic));
    int ridx, rate_flags;
    uint8_t cmdver;
    uint8_t hwrate;

    *flags = 0;
    static const int awdlRateIndexes[ItlAWDL::kLegacyRateTop + 1] =
        {IWX_RATE_12M_INDEX, IWX_RATE_24M_INDEX, IWX_RATE_36M_INDEX, IWX_RATE_54M_INDEX};
    if ((ic->ic_opmode == IEEE80211_M_MONITOR && awdlDataInFlight == 3) || awdlCoexBaSendingAgg)
        return &iwx_rates[IWX_RATE_MCS_7_INDEX];
    const bool awdlUnicastData = ic->ic_opmode == IEEE80211_M_MONITOR && awdlDataInFlight == 2 &&
        type == IEEE80211_FC0_TYPE_DATA && !IEEE80211_IS_MULTICAST(wh->i_addr1);
    int awdlMcs = ItlAWDL::htMcsForStep(awdlDataRate.step);
    if (awdlUnicastData && awdlMcs >= 0) {
        // A fixed HT rate, 20 MHz, long guard interval. Two streams need both antennas.
        const uint8_t antennas = IWX_RATE_MCS_ANT_AB_MSK >> IWX_RATE_MCS_ANT_POS;
        if (awdlMcs > 7 && (iwx_fw_valid_tx_ant(sc) & antennas) != antennas)
            awdlMcs = 7;
        const iwx_rate *awdlInfo = &iwx_rates[iwx_mcs2ridx[awdlMcs]];
        uint32_t awdlFlags = awdlMcs > 7 ? IWX_RATE_MCS_ANT_AB_MSK : iwx_get_tx_ant(sc, ni, awdlInfo, type, wh);
        const uint8_t awdlCmdVer = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_TX_CMD);
        if (awdlCmdVer > 8 && awdlCmdVer != IWX_FW_CMD_VER_UNKNOWN)
            awdlFlags |= IWX_RATE_MCS_HT_MSK | (awdlMcs > 7 ? IWX_RATE_MCS_NSS_MSK : 0U) | uint32_t(awdlMcs & IWX_RATE_HT_MCS_CODE_MSK);
        else
            awdlFlags |= IWX_RATE_MCS_HT_MSK_V1 | uint32_t(awdlMcs);
        *flags |= IWX_TX_FLAGS_CMD_RATE;
        *rate_n_flags = htole32(awdlFlags);
        return awdlInfo;
    }
    // Stays on this step's rate only if the station's rate set has it, otherwise the next lower.
    unsigned awdlStep = awdlMcs >= 0 ? ItlAWDL::kLegacyRateTop : awdlDataRate.step;
    while (awdlStep && !iwx_ridx2rate(rs, awdlRateIndexes[awdlStep]))
        --awdlStep;
    const int awdlDataRidx = awdlRateIndexes[awdlStep];
    if (awdlCoexSending) {
        // The listener's station has no rate table: a fixed OFDM rate valid on either band. 6 Mbit/s for
        // group and management frames, 24 Mbit/s for unicast data.
        ridx = (IEEE80211_IS_MULTICAST(wh->i_addr1) || type != IEEE80211_FC0_TYPE_DATA) ?
            IWX_RATE_6M_INDEX : IWX_RATE_24M_INDEX;
        *flags |= IWX_TX_FLAGS_CMD_RATE;
        const uint8_t vhtCmdVer = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_TX_CMD);
        const uint8_t bothAntennas = IWX_RATE_MCS_ANT_AB_MSK >> IWX_RATE_MCS_ANT_POS;
        if (ridx == IWX_RATE_24M_INDEX && awdlCoexRateStep >= 4 && awdlCoexRateStep < nitems(kAwdlCoexRateMcs) &&
            awdlCoexOwnWidth == 80 && vhtCmdVer > 8 && vhtCmdVer != IWX_FW_CMD_VER_UNKNOWN &&
            (iwx_fw_valid_tx_ant(sc) & bothAntennas) == bothAntennas) {
            // VHT, 80 MHz, two streams, as dedicated mode's fixed rate for its 80 MHz sends.
            *rate_n_flags = htole32(IWX_RATE_MCS_VHT_MSK | IWX_RATE_MCS_CHAN_WIDTH_80 | IWX_RATE_MCS_NSS_MSK |
                                    IWX_RATE_MCS_ANT_AB_MSK | uint32_t(kAwdlCoexRateMcs[awdlCoexRateStep]));
            ++awdlCoexVhtFrames;
            return &iwx_rates[IWX_RATE_MCS_7_INDEX];
        }
        if (ridx == IWX_RATE_24M_INDEX && awdlCoexRateStep >= 4)
            awdlCoexRateStep = 3; // Not at 80 MHz any more, or no VHT command format: back to HT.
        if (ridx == IWX_RATE_24M_INDEX && awdlCoexRateStep > 0 && awdlCoexRateStep < 4) {
            // A fixed HT rate, 20 MHz, one stream, long guard interval, as dedicated mode sends.
            const unsigned mcs = kAwdlCoexRateMcs[awdlCoexRateStep];
            const iwx_rate *htInfo = &iwx_rates[iwx_mcs2ridx[mcs]];
            uint32_t htFlags = iwx_get_tx_ant(sc, ni, htInfo, type, wh);
            const uint8_t htCmdVer = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_TX_CMD);
            if (htCmdVer > 8 && htCmdVer != IWX_FW_CMD_VER_UNKNOWN)
                htFlags |= IWX_RATE_MCS_HT_MSK | uint32_t(mcs & IWX_RATE_HT_MCS_CODE_MSK);
            else
                htFlags |= IWX_RATE_MCS_HT_MSK_V1 | uint32_t(mcs);
            *rate_n_flags = htole32(htFlags);
            return htInfo;
        }
    } else if (awdlUnicastData && iwx_ridx2rate(rs, awdlDataRidx)) {
        // macOS's unicast AWDL data, not a probe: the lowest rate would limit a transfer.
        ridx = awdlDataRidx;
        *flags |= IWX_TX_FLAGS_CMD_RATE;
    // The monitor station has no rate table, so its unicast data needs a fixed rate too.
    } else if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
        type != IEEE80211_FC0_TYPE_DATA || ic->ic_opmode == IEEE80211_M_MONITOR) {
        /* for non-data, use the lowest supported rate */
        // The listener's frames: OFDM 6 Mbit/s, valid on either band, whatever the BSS's lowest rate is.
        ridx = min_ridx;
        *flags |= IWX_TX_FLAGS_CMD_RATE;
    } else if (ic->ic_fixed_mcs != -1) {
        ridx = sc->sc_fixed_ridx;
        *flags |= IWX_TX_FLAGS_CMD_RATE;
    } else if (ic->ic_fixed_rate != -1) {
        ridx = sc->sc_fixed_ridx;
        *flags |= IWX_TX_FLAGS_CMD_RATE;
    } else if (ni->ni_flags & IEEE80211_NODE_HT) {
        ridx = iwx_mcs2ridx[ni->ni_txmcs];
        return &iwx_rates[ridx];
    } else {
        uint8_t rval;
        rval = (rs->rs_rates[ni->ni_txrate] & IEEE80211_RATE_VAL);
        ridx = iwx_rval2ridx(rval);
        if (ridx < min_ridx)
            ridx = min_ridx;
        return &iwx_rates[ridx];
    }
    
    rinfo = &iwx_rates[ridx];
    
    rate_flags = iwx_get_tx_ant(sc, ni, rinfo, type, wh);
    
    hwrate = rinfo->plcp;
    cmdver = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_TX_CMD);
    if (cmdver > 8 && cmdver != IWX_FW_CMD_VER_UNKNOWN) {
        if (IWX_RIDX_IS_CCK(ridx)) {
            rate_flags |= IWX_RATE_MCS_CCK_MSK;
            hwrate = ridx;
        } else {
            rate_flags |= IWX_RATE_MCS_LEGACY_OFDM_MSK;
            hwrate = awdlCoexSending ? iwx_rate2idx(iwx_rates[ridx].rate) :
                iwx_rate2idx(iwx_ridx2rate(rs, ridx) & IEEE80211_RATE_VAL);
        }
    } else if (IWX_RIDX_IS_CCK(ridx))
        rate_flags |= IWX_RATE_MCS_CCK_MSK_V1;
    // A per-frame line would flush the startup log during a monitor train.
    if (ic->ic_opmode != IEEE80211_M_MONITOR && !awdlCoexSending)
        XYLog("%s hwrate: %d ridx: %d rate: %d minrate: %d\n", __FUNCTION__, hwrate, ridx, iwx_ridx2rate(rs, ridx) & IEEE80211_RATE_VAL, ieee80211_min_basic_rate(ic));
    *rate_n_flags = htole32(rate_flags | hwrate);
    
    return rinfo;
}

void ItlIwx::
iwx_tx_update_byte_tbl
