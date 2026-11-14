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
iwx_rx_pkt(struct iwx_softc *sc, struct iwx_rx_data *data, struct mbuf_list *ml)
{
    struct _ifnet *ifp = IC2IFP(&sc->sc_ic);
    struct iwx_rx_packet *pkt, *nextpkt;
    uint32_t offset = 0, nextoff = 0, nmpdu = 0, len;
    mbuf_t m0, m;
    const size_t minsz = sizeof(pkt->len_n_flags) + sizeof(pkt->hdr);
    int qid, idx, code, handled;
    
    //    bus_dmamap_sync(sc->sc_dmat, data->map, 0, IWX_RBUF_SIZE,
    //        BUS_DMASYNC_POSTREAD);
    
    const uint8_t *rxbuf = (const uint8_t *)bus_dmamap_rx_data(data->map, IWX_RBUF_SIZE);
    if (rxbuf) {
        /* Copy only the packets the firmware wrote, zero the rest. */
        if (!bus_dmamap_read_used(data->map, data->m,
            iwx_rx_buf_used(sc, rxbuf), IWX_RBUF_SIZE)) {
            ifp->netStat->inputErrors++;
            return;
        }
    } else if (!bus_dmamap_read(data->map, data->m, IWX_RBUF_SIZE)) {
        ifp->netStat->inputErrors++;
        return;
    }
    m0 = data->m;
    while (m0 && offset + minsz < IWX_RBUF_SIZE) {
        handled = 1;
        pkt = (struct iwx_rx_packet *)((uint8_t*)mbuf_data(m0) + offset);
        qid = pkt->hdr.qid;
        idx = pkt->hdr.idx;
        
        code = IWX_WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd);
        
        DPRINTFN(3, ("%s gid=%d cmd=%d code=0x%02x qid=%d idx=%d packet_len=%d payload_len=%d\n", __FUNCTION__, pkt->hdr.group_id, pkt->hdr.cmd, code, qid, idx, iwx_rx_packet_len(pkt), iwx_rx_packet_payload_len(pkt)));
        
        if (!iwx_rx_pkt_valid(pkt))
            break;
        
        /*
         * XXX Intel inside (tm)
         * Any commands in the LONG_GROUP could actually be in the
         * LEGACY group. Firmware API versions >= 50 reject commands
         * in group 0, forcing us to use this hack.
         */
        if (iwx_cmd_groupid(code) == IWX_LONG_GROUP &&
            qid == IWX_DQA_CMD_QUEUE && idx < sc->txq[qid].ring_count) {
            struct iwx_tx_ring *ring = &sc->txq[qid];
            struct iwx_tx_data *txdata = &ring->data[idx];
            if (txdata->flags & IWX_TXDATA_FLAG_CMD_IS_NARROW)
                code = iwx_cmd_opcode(code);
        }
        
        len = sizeof(pkt->len_n_flags) + iwx_rx_packet_len(pkt);
        if (len < minsz || len > (IWX_RBUF_SIZE - offset))
            break;
        
        if (code == IWX_REPLY_RX_MPDU_CMD)
            awdlCoexBaRxRecord(sc, pkt->data, iwx_rx_packet_payload_len(pkt), 0);

        if (code == IWX_REPLY_RX_MPDU_CMD && ++nmpdu == 1) {
            /* Take mbuf m0 off the RX ring. */
            if (iwx_rx_addbuf(sc, IWX_RBUF_SIZE, sc->rxq.cur)) {
                ifp->netStat->inputErrors++;
                break;
            }
            KASSERT(data->m != m0, "data->m != m0");
        }
        
        if (code == IWX_STATISTICS_NOTIFICATION || code == IWX_STATISTICS_CMD) {
            const size_t length = iwx_rx_packet_payload_len(pkt);
            const bool observe = awdlRadioProbe && (sc->sc_flags & IWX_FLAG_MAC_ACTIVE) &&
                sc->sc_ic.ic_opmode == IEEE80211_M_MONITOR && sc->sc_ic.ic_state == IEEE80211_S_RUN &&
                !awdlBackgroundScanActive && !awdlCountryDiscovery;
            if (observe) {
                if (code == IWX_STATISTICS_CMD)
                    awdlRadioLastCommandLength = uint32_t(length);
                else
                    awdlRadioLastNotificationLength = uint32_t(length);
                if (isset(sc->sc_ucode_api, IWX_UCODE_TLV_API_NEW_RX_STATS) &&
                    length == sizeof(iwx_notif_statistics)) {
                    const struct iwx_notif_statistics *stats;
                    SYNC_RESP_STRUCT(stats, pkt, const struct iwx_notif_statistics *);
                    awdlNoteRadioStatistics(stats->general.common);
                    awdlRadioReportUS = awdlUptimeUS();
                } else if (!isset(sc->sc_ucode_api, IWX_UCODE_TLV_API_NEW_RX_STATS) &&
                           length == sizeof(iwx_notif_statistics_v11)) {
                    const struct iwx_notif_statistics_v11 *stats;
                    SYNC_RESP_STRUCT(stats, pkt, const struct iwx_notif_statistics_v11 *);
                    awdlNoteRadioStatistics(stats->general.common);
                    awdlRadioReportUS = awdlUptimeUS();
                } else if (code == IWX_STATISTICS_NOTIFICATION || length > sizeof(uint32_t)) {
                    ++awdlRadioUnexpectedLengths;
                }
            }
        }
        switch (code) {
            case IWX_REPLY_RX_PHY_CMD:
                iwx_rx_rx_phy_cmd(sc, pkt, data);
                break;
                
            case IWX_REPLY_RX_MPDU_CMD: {
                // This notification's own payload length (checked above against the buffer), not the
                // space left in the buffer: a firmware record can't then reach into the next one.
                size_t maxlen = iwx_rx_packet_payload_len(pkt);
                nextoff = offset +
                roundup(len, IWX_FH_RSCSR_FRAME_ALIGN);
                nextpkt = (struct iwx_rx_packet *)
                ((uint8_t*)mbuf_data(m0) + nextoff);
                if (nextoff + minsz >= IWX_RBUF_SIZE ||
                    !iwx_rx_pkt_valid(nextpkt)) {
                    /* No need to copy last frame in buffer. */
                    if (offset > 0)
                        mbuf_adj(m0, offset);
                    iwx_rx_mpdu_mq(sc, m0, pkt->data, maxlen, ml);
                    m0 = NULL; /* stack owns m0 now; abort loop */
                } else {
                    /*
                     * Create an mbuf which points to the current
                     * packet. Always copy from offset zero to
                     * preserve m_pkthdr.
                     */
                    mbuf_copym(m0, 0, MBUF_COPYALL, MBUF_DONTWAIT, &m);
                    if (m == NULL) {
                        ifp->netStat->inputErrors++;
                        mbuf_freem(m0);
                        m0 = NULL;
                        break;
                    }
                    mbuf_adj(m, offset);
                    iwx_rx_mpdu_mq(sc, m, pkt->data, maxlen, ml);
                }
                break;
            }
            case IWX_BA_NOTIF:
                iwx_rx_tx_ba_notif(sc, pkt, data);
                break;
                
            case IWX_TX_CMD:
                iwx_rx_tx_cmd(sc, pkt, data);
                break;
                
            case IWX_MISSED_BEACONS_NOTIFICATION:
                iwx_rx_bmiss(sc, pkt, data);
                break;
                
            case IWX_MFUART_LOAD_NOTIFICATION:
                break;
                
            case IWX_ALIVE: {
                struct iwx_alive_resp_v4 *resp4;
                struct iwx_alive_resp_v5 *resp5;
                
                DPRINTF(("%s: firmware alive, size=%d\n", __FUNCTION__, iwx_rx_packet_payload_len(pkt)));
                if (iwx_rx_packet_payload_len(pkt) == sizeof(*resp4)) {
                    SYNC_RESP_STRUCT(resp4, pkt, struct iwx_alive_resp_v4 *);
                    sc->sc_uc.uc_lmac_error_event_table[0] = le32toh(
                                                                     resp4->lmac_data[0].dbg_ptrs.error_event_table_ptr);
                    sc->sc_uc.uc_lmac_error_event_table[1] = le32toh(
                                                                     resp4->lmac_data[1].dbg_ptrs.error_event_table_ptr);
                    sc->sc_uc.uc_log_event_table = le32toh(
                                                           resp4->lmac_data[0].dbg_ptrs.log_event_table_ptr);
                    sc->sc_uc.uc_umac_error_event_table = le32toh(
                                                                  resp4->umac_data.dbg_ptrs.error_info_addr);
                    if (resp4->status == IWX_ALIVE_STATUS_OK)
                        sc->sc_uc.uc_ok = 1;
                    else
                        sc->sc_uc.uc_ok = 0;
                } else {
                    SYNC_RESP_STRUCT(resp5, pkt, struct iwx_alive_resp_v5 *);
                    sc->sc_uc.uc_lmac_error_event_table[0] = le32toh(
                                                                     resp5->lmac_data[0].dbg_ptrs.error_event_table_ptr);
                    sc->sc_uc.uc_lmac_error_event_table[1] = le32toh(
                                                                     resp5->lmac_data[1].dbg_ptrs.error_event_table_ptr);
                    sc->sc_uc.uc_log_event_table = le32toh(
                                                           resp5->lmac_data[0].dbg_ptrs.log_event_table_ptr);
                    sc->sc_uc.uc_umac_error_event_table = le32toh(
                                                                  resp5->umac_data.dbg_ptrs.error_info_addr);
                    if (resp5->status == IWX_ALIVE_STATUS_OK)
                        sc->sc_uc.uc_ok = 1;
                    else
                        sc->sc_uc.uc_ok = 0;
                    
                    sc->sku_id[0] = le32toh(resp5->sku_id.data[0]);
                    sc->sku_id[1] = le32toh(resp5->sku_id.data[1]);
                    sc->sku_id[2] = le32toh(resp5->sku_id.data[2]);
                    
                    XYLog("Got sku_id: 0x0%x 0x0%x 0x0%x\n",
                                 sc->sku_id[0],
                                 sc->sku_id[1],
                                 sc->sku_id[2]);
                }
                
                sc->sc_uc.uc_intr = 1;
                getMainCommandGate()->commandWakeup(&sc->sc_uc.uc_intr);
                break;
            }
                
            case IWX_STATISTICS_NOTIFICATION: {
                struct iwx_notif_statistics *stats;
                struct iwx_notif_statistics_v11 *stats_v11;
                if (isset(sc->sc_ucode_api, IWX_UCODE_TLV_API_NEW_RX_STATS)) {
                    if (iwx_rx_packet_payload_len(pkt) < sizeof(*stats))
                        break;
                    SYNC_RESP_STRUCT(stats, pkt, struct iwx_notif_statistics *);
                    sc->sc_noise = iwx_get_noise((uint8_t *)&stats->rx.general.beacon_silence_rssi_a);
                } else {
                    if (iwx_rx_packet_payload_len(pkt) < sizeof(*stats_v11))
                        break;
                    SYNC_RESP_STRUCT(stats_v11, pkt, struct iwx_notif_statistics_v11 *);
                    sc->sc_noise = iwx_get_noise((uint8_t *)&stats_v11->rx.general.beacon_silence_rssi_a);
                }
                break;
            }
                
            case IWX_DTS_MEASUREMENT_NOTIFICATION:
            case IWX_WIDE_ID(IWX_PHY_OPS_GROUP,
                             IWX_DTS_MEASUREMENT_NOTIF_WIDE):
            case IWX_WIDE_ID(IWX_PHY_OPS_GROUP,
                             IWX_TEMP_REPORTING_THRESHOLDS_CMD):
                break;
                
            case IWX_WIDE_ID(IWX_PHY_OPS_GROUP,
                             IWX_CT_KILL_NOTIFICATION): {
                struct iwx_ct_kill_notif *notif;
                SYNC_RESP_STRUCT(notif, pkt, struct iwx_ct_kill_notif *);
                XYLog("%s: device at critical temperature (%u degC), "
                       "stopping device\n",
                       DEVNAME(sc), le16toh(notif->temperature));
                sc->sc_flags |= IWX_FLAG_HW_ERR;
                task_add(systq, &sc->init_task);
                break;
            }
                
            case IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP,
                IWX_NVM_GET_INFO):
            case IWX_ADD_STA_KEY:
            case IWX_PHY_CONFIGURATION_CMD:
            case IWX_TX_ANT_CONFIGURATION_CMD:
            case IWX_ADD_STA:
            case IWX_MAC_CONTEXT_CMD:
            case IWX_REPLY_SF_CFG_CMD:
            case IWX_POWER_TABLE_CMD:
            case IWX_LTR_CONFIG:
            case IWX_PHY_CONTEXT_CMD:
            case IWX_BINDING_CONTEXT_CMD:
            case IWX_WIDE_ID(IWX_LONG_GROUP, IWX_SCAN_CFG_CMD):
            case IWX_WIDE_ID(IWX_LONG_GROUP, IWX_SCAN_REQ_UMAC):
            case IWX_WIDE_ID(IWX_LONG_GROUP, IWX_SCAN_ABORT_UMAC):
            case IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_SESSION_PROTECTION_CMD):
            case IWX_REPLY_BEACON_FILTERING_CMD:
            case IWX_MAC_PM_POWER_TABLE:
            case IWX_TIME_QUOTA_CMD:
            case IWX_REMOVE_STA:
            case IWX_TXPATH_FLUSH:
            case IWX_BT_CONFIG:
            case IWX_MCC_UPDATE_CMD:
            case IWX_TIME_EVENT_CMD:
            case IWX_STATISTICS_CMD:
            case IWX_SCD_QUEUE_CFG: {
                size_t pkt_len;
                
                if (qid != IWX_DQA_CMD_QUEUE ||
                    idx >= sc->txq[IWX_DQA_CMD_QUEUE].ring_count ||
                    !(sc->txq[IWX_DQA_CMD_QUEUE].data[idx].flags & IWX_TXDATA_FLAG_CMD_PENDING) ||
                    sc->sc_cmd_resp_pkt[idx] == NULL)
                    break;
                
                //            bus_dmamap_sync(sc->sc_dmat, data->map, 0,
                //                sizeof(*pkt), BUS_DMASYNC_POSTREAD);
                
                pkt_len = sizeof(pkt->len_n_flags) +
                iwx_rx_packet_len(pkt);
                
                if ((pkt->hdr.group_id & IWX_CMD_FAILED_MSK) ||
                    pkt_len < sizeof(*pkt) ||
                    pkt_len > sc->sc_cmd_resp_len[idx]) {
                    ::free(sc->sc_cmd_resp_pkt[idx]);
                    sc->sc_cmd_resp_pkt[idx] = NULL;
                    break;
                }
                
                //            bus_dmamap_sync(sc->sc_dmat, data->map, sizeof(*pkt),
                //                pkt_len - sizeof(*pkt), BUS_DMASYNC_POSTREAD);
                memcpy(sc->sc_cmd_resp_pkt[idx], pkt, pkt_len);
                break;
            }
                
            case IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP, IWX_PNVM_INIT_COMPLETE_NTFY):
                sc->sc_init_complete |= IWX_PNVM_COMPLETE;
                getMainCommandGate()->commandWakeup(&sc->sc_init_complete);
                struct iwl_pnvm_init_complete_ntfy *pnvm_ntf;
                SYNC_RESP_STRUCT(pnvm_ntf, pkt, struct iwl_pnvm_init_complete_ntfy *);
                XYLog("PNVM complete notification received with status 0x%0x\n", le32toh(pnvm_ntf->status));
                break;
                
            case IWX_INIT_COMPLETE_NOTIF:
                sc->sc_init_complete |= IWX_INIT_COMPLETE;
                getMainCommandGate()->commandWakeup(&sc->sc_init_complete);
                break;
                
            case IWX_SCAN_COMPLETE_UMAC: {
                struct iwx_umac_scan_complete *notif;
                SYNC_RESP_STRUCT(notif, pkt, struct iwx_umac_scan_complete *);
                awdlBackgroundScanActive = false;
                if (awdlCountryDiscovery) {
                    awdlCountryScanDone = true;
                    getMainCommandGate()->commandWakeup(&awdlCountryDiscovery);
                }
                iwx_endscan(sc);
                break;
            }
                
            case IWX_SCAN_ITERATION_COMPLETE_UMAC: {
                struct iwx_umac_scan_iter_complete_notif *notif;
                SYNC_RESP_STRUCT(notif, pkt, struct iwx_umac_scan_iter_complete_notif *);
                awdlBackgroundScanActive = false;
                if (awdlCountryDiscovery) {
                    awdlCountryScanDone = true;
                    getMainCommandGate()->commandWakeup(&awdlCountryDiscovery);
                }
                iwx_endscan(sc);
                break;
            }
                
            case IWX_MCC_CHUB_UPDATE_CMD: {
                struct iwx_mcc_chub_notif *notif;
                SYNC_RESP_STRUCT(notif, pkt, struct iwx_mcc_chub_notif *);
                iwx_mcc_update(sc, notif);
                break;
            }
                
            case IWX_REPLY_ERROR: {
                struct iwx_error_resp *resp;
                SYNC_RESP_STRUCT(resp, pkt, struct iwx_error_resp *);
                XYLog("%s: firmware error 0x%x, cmd 0x%x\n",
                      DEVNAME(sc), le32toh(resp->error_type),
                      resp->cmd_id);
                break;
            }
                
            case IWX_TIME_EVENT_NOTIFICATION: {
                struct iwx_time_event_notif *notif;
                uint32_t action;
                SYNC_RESP_STRUCT(notif, pkt, struct iwx_time_event_notif *);
                
                if (sc->sc_time_event_uid != le32toh(notif->unique_id))
                    break;
                action = le32toh(notif->action);
                if (action & IWX_TE_V2_NOTIF_HOST_EVENT_END)
                    sc->sc_flags &= ~IWX_FLAG_TE_ACTIVE;
                break;
            }
                
            case IWX_WIDE_ID(IWX_SYSTEM_GROUP,
                             IWX_FSEQ_VER_MISMATCH_NOTIFICATION):
                break;
                
                /*
                 * Firmware versions 21 and 22 generate some DEBUG_LOG_MSG
                 * messages. Just ignore them for now.
                 */
            case IWX_DEBUG_LOG_MSG:
                break;
                
            case IWX_MCAST_FILTER_CMD:
                break;
                
            case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_DQA_ENABLE_CMD):
                break;
                
            case IWX_WIDE_ID(IWX_SYSTEM_GROUP, IWX_SOC_CONFIGURATION_CMD):
                break;
                
            case IWX_WIDE_ID(IWX_SYSTEM_GROUP, IWX_INIT_EXTENDED_CFG_CMD):
                break;
                
            case IWX_WIDE_ID(IWX_SYSTEM_GROUP, IWX_SHARED_MEM_CFG_CMD):
                break;
                
            case IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP,
                             IWX_NVM_ACCESS_COMPLETE):
                break;
                
            case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_RX_NO_DATA_NOTIF):
                break; /* happens in monitor mode; ignore for now */
            case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_TLC_MNG_CONFIG_CMD):
                break;
            case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_STA_HE_CTXT_CMD):
                /* Empty command ACK; complete the synchronous HE setup below. */
                break;
                
            case IWX_WIDE_ID(IWX_DATA_PATH_GROUP,
                             IWX_TLC_MNG_UPDATE_NOTIF): {
                struct iwx_tlc_update_notif *notif;
                SYNC_RESP_STRUCT(notif, pkt, struct iwx_tlc_update_notif *);
                if (iwx_rx_packet_payload_len(pkt) == sizeof(*notif))
                    iwx_rs_update(sc, notif);
                break;
            }
                
            case IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_SESSION_PROTECTION_NOTIF): {
                struct iwx_session_prot_notif *notif;
                SYNC_RESP_STRUCT(notif, pkt, struct iwx_session_prot_notif *);
                if (!notif->status) {
                    XYLog("Session protection failure\n");
                    sc->sc_flags &= ~IWX_FLAG_TE_ACTIVE;
                    break;
                }
                if (!notif->start) {
                    /*
                     * By now, we should have finished association
                     * and know the dtim period.
                     */
                    sc->sc_flags &= ~IWX_FLAG_TE_ACTIVE;
                    XYLog("%s finish association\n", __FUNCTION__);
                }
                break;
            }
            
            case IWX_WIDE_ID(IWX_LEGACY_GROUP, IWX_BAR_FRAME_RELEASE): {
                struct iwx_bar_frame_release *release;
                SYNC_RESP_STRUCT(release, pkt, struct iwx_bar_frame_release *);
                unsigned int baid = le32_get_bits(release->ba_info,
                                  IWX_BAR_FRAME_RELEASE_BAID_MASK);
                unsigned int nssn = le32_get_bits(release->ba_info,
                                  IWX_BAR_FRAME_RELEASE_NSSN_MASK);
                unsigned int sta_id = le32_get_bits(release->sta_tid,
                                    IWX_BAR_FRAME_RELEASE_STA_MASK);
                unsigned int tid = le32_get_bits(release->sta_tid,
                                 IWX_BAR_FRAME_RELEASE_TID_MASK);
                struct iwx_rxba_data *baid_data;
                
                if (iwx_rx_packet_payload_len(pkt) < sizeof(*release))
                    break;
                
                if (baid == IWX_RX_REORDER_DATA_INVALID_BAID || baid >= IWX_MAX_BAID)
                    break;
                
                baid_data = &sc->sc_rxba_data[baid];
                
                if (!baid_data) {
                    DPRINTFN(1, ("Got valid BAID %d but not allocated, invalid BAR release!\n",
                     baid));
                    break;
                }
                
                if (sta_id == kAwdlBaStationId)
                    ++awdlBaBarReleases;
                if (tid != baid_data->tid || sta_id != baid_data->sta_id) {
                    DPRINTFN(1, ("baid 0x%x is mapped to sta:%d tid:%d, but BAR release received for sta:%d tid:%d\n",
                                 baid, baid_data->sta_id, baid_data->tid, sta_id,
                                 tid));
                    break;
                }
                
                iwx_release_frames(sc, sc->sc_ic.ic_bss, baid_data, &baid_data->reorder_buf, nssn, ml);
                
                break;
            }
                
            default:
                handled = 0;
                XYLog("%s: unhandled firmware response 0x%x/0x%x "
                      "rx ring %d[%d]\n",
                      DEVNAME(sc), code, pkt->len_n_flags,
                      (qid & ~0x80), idx);
                break;
        }
        
        /*
         * uCode sets bit 0x80 when it originates the notification,
         * i.e. when the notification is not a direct response to a
         * command sent by the driver.
         * For example, uCode issues IWX_REPLY_RX when it sends a
         * received frame to the driver.
         */
        if (handled && !(qid & (1 << 7))) {
            iwx_cmd_done(sc, qid, idx, code);
        }
        
        offset += roundup(len, IWX_FH_RSCSR_FRAME_ALIGN);
        
        if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)
            break;
    }
    
    if (m0 && m0 != data->m && mbuf_type(m0) != MBUF_TYPE_FREE)
        mbuf_freem(m0);
}

void ItlIwx::
iwx_rx_mpdu_mq(struct iwx_softc *sc, mbuf_t m, void *pktdata,
               size_t maxlen, struct mbuf_list *ml)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct ieee80211_rxinfo rxi;
    struct iwx_rx_mpdu_desc *desc;
    uint32_t len, hdrlen, rate_n_flags, device_timestamp;
    int rssi;
    uint8_t chanidx;
    uint16_t phy_info;
    size_t desc_size;
    
    desc = (struct iwx_rx_mpdu_desc *)pktdata;
    if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)
        desc_size = sizeof(struct iwx_rx_mpdu_desc);
    else
        desc_size = IWX_RX_DESC_SIZE_V1;
    
    awdlCoexBaRxRecord(sc, pktdata, maxlen, 1);

    // The whole descriptor must be present before any field of it is read (security audit C02).
    if (maxlen < desc_size) {
        IC2IFP(ic)->netStat->inputErrors++;
        mbuf_freem(m);
        return;
    }
    if (!(desc->status & htole16(IWX_RX_MPDU_RES_STATUS_CRC_OK)) ||
        !(desc->status & htole16(IWX_RX_MPDU_RES_STATUS_OVERRUN_OK))) {
        mbuf_freem(m);
        return; /* drop */
    }
    
    awdlCoexBaRxRecord(sc, pktdata, maxlen, 2);
    len = le16toh(desc->mpdu_len);
    if (ic->ic_opmode == IEEE80211_M_MONITOR) {
        /* Allow control frames in monitor mode. */
        if (len < sizeof(struct ieee80211_frame_cts)) {
            ic->ic_stats.is_rx_tooshort++;
            IC2IFP(ic)->netStat->inputErrors++;
            mbuf_freem(m);
            return;
        }
    } else if (len < sizeof(struct ieee80211_frame)) {
        ic->ic_stats.is_rx_tooshort++;
        IC2IFP(ic)->netStat->inputErrors++;
        mbuf_freem(m);
        return;
    }
    if (len > maxlen - desc_size) {
        IC2IFP(ic)->netStat->inputErrors++;
        mbuf_freem(m);
        return;
    }
    
    //    m->m_data = pktdata + sizeof(*desc);
    //    m->m_pkthdr.len = m->m_len = len;
    mbuf_setdata(m, (uint8_t*)pktdata + desc_size, len);
    mbuf_pkthdr_setlen(m, len);
    mbuf_setlen(m, len);
    
    /* Account for padding following the frame header. */
    if (desc->mac_flags2 & IWX_RX_MPDU_MFLG2_PAD) {
        struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
        int type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
        if (type == IEEE80211_FC0_TYPE_CTL) {
            switch (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
                case IEEE80211_FC0_SUBTYPE_CTS:
                    hdrlen = sizeof(struct ieee80211_frame_cts);
                    break;
                case IEEE80211_FC0_SUBTYPE_ACK:
                    hdrlen = sizeof(struct ieee80211_frame_ack);
                    break;
                default:
                    hdrlen = sizeof(struct ieee80211_frame_min);
                    break;
            }
        } else
            hdrlen = ieee80211_get_hdrlen(wh);
        
        if ((le16toh(desc->status) &
             IWX_RX_MPDU_RES_STATUS_SEC_ENC_MSK) ==
            IWX_RX_MPDU_RES_STATUS_SEC_CCM_ENC) {
            /* Padding is inserted after the IV. */
            hdrlen += IEEE80211_CCMP_HDRLEN;
        }
        // The header, its IV and the two padding bytes must all lie inside the frame.
        if (hdrlen + 2 > len) {
            ic->ic_stats.is_rx_tooshort++;
            IC2IFP(ic)->netStat->inputErrors++;
            mbuf_freem(m);
            return;
        }
        
        //        memmove(m->m_data + 2, m->m_data, hdrlen);
        memmove((uint8_t*)mbuf_data(m) + 2, mbuf_data(m), hdrlen);
        mbuf_adj(m, 2);
    }
    
    awdlCoexBaRxMbuf(sc, m, desc, 3);
    memset(&rxi, 0, sizeof(rxi));
    
    /*
     * Hardware de-aggregates A-MSDUs and copies the same MAC header
     * in place for each subframe. But it leaves the 'A-MSDU present'
     * bit set in the frame header. We need to clear this bit ourselves.
     * (XXX This workaround is not required on AX200/AX201 devices that
     * have been tested by me, but it's unclear when this problem was
     * fixed in the hardware. It definitely affects the 9k generation.
     * Leaving this in place for now since some 9k/AX200 hybrids seem
     * to exist that we may eventually add support for.)
     *
     * And we must allow the same CCMP PN for subframes following the
     * first subframe. Otherwise they would be discarded as replays.
     */
    if (desc->mac_flags2 & IWX_RX_MPDU_MFLG2_AMSDU) {
        struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
        uint8_t subframe_idx = (desc->amsdu_info &
                                IWX_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK);
        if (subframe_idx > 0)
            rxi.rxi_flags |= IEEE80211_RXI_HWDEC_SAME_PN;
        if (ieee80211_has_qos(wh) && ieee80211_has_addr4(wh) &&
            mbuf_len(m) >= sizeof(struct ieee80211_qosframe_addr4)) {
            struct ieee80211_qosframe_addr4 *qwh4 = mtod(m,
                                                         struct ieee80211_qosframe_addr4 *);
            qwh4->i_qos[0] &= htole16(~IEEE80211_QOS_AMSDU);
        } else if (ieee80211_has_qos(wh) &&
                   mbuf_len(m) >= sizeof(struct ieee80211_qosframe)) {
            struct ieee80211_qosframe *qwh = mtod(m,
                                                  struct ieee80211_qosframe *);
            qwh->i_qos[0] &= htole16(~IEEE80211_QOS_AMSDU);
        }    
    }
    
    /*
     * Verify decryption before duplicate detection. The latter uses
     * the TID supplied in QoS frame headers and this TID is implicitly
     * verified as part of the CCMP nonce.
     */
    if (iwx_rx_hwdecrypt(sc, m, le16toh(desc->status), &rxi)) {
        mbuf_freem(m);
        return;
    }
    
    awdlCoexBaRxMbuf(sc, m, desc, 4);
    bool awdlPeerData = false;
    if (awdlRx && mbuf_len(m) >= 24 && ItlAWDL::knownAddress(awdlOwnAddress)) {
        const uint8_t *f = mtod(m, const uint8_t *);
        awdlPeerData = (f[0] & 0x0c) == 8 && !(f[0] & 0x40) && ItlAWDL::sameAddress(f + 4, awdlOwnAddress);
        if (awdlPeerData) {
            const bool qos = (f[0] & 0x80) && mbuf_len(m) >= 26;
            if (!qos)
                ++awdlRxSequences.counts.noQos;
            awdlRxSequences.note(qos ? f[24] & 7U : 0U, uint16_t((f[22] | f[23] << 8) >> 4), f[1] & 0x08, mbuf_pkthdr_len(m));
            const uint16_t framePhy = le16toh(desc->phy_info);
            if (framePhy & IWX_RX_MPDU_PHY_AMPDU)
                awdlRxSequences.noteAmpdu(framePhy & IWX_RX_MPDU_PHY_AMPDU_TOGGLE);
        }
    }
    if (iwx_detect_duplicate(sc, m, desc, &rxi)) {
        if (awdlPeerData)
            ++awdlRxSequences.counts.duplicatesDropped;
        mbuf_freem(m);
        return;
    }
    
    awdlCoexBaRxMbuf(sc, m, desc, 5);
    rssi = iwx_rxmq_get_signal_strength(sc, desc);
    rssi = (0 - IWX_MIN_DBM) + rssi;    /* normalize */
    rssi = MIN(rssi, ic->ic_max_rssi);    /* clip to max. 100% */
    
    rxi.rxi_rssi = rssi;
    
    phy_info = le16toh(desc->phy_info);
    awdlRxPhyInfo = phy_info;
    if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
        rate_n_flags = le32toh(desc->v3.rate_n_flags);
        chanidx = desc->v3.channel;
        device_timestamp = desc->v3.gp2_on_air_rise;
        rxi.rxi_tstamp = (uint32_t)le64toh(desc->v3.tsf_on_air_rise);
    } else {
        rate_n_flags = le32toh(desc->v1.rate_n_flags);
        chanidx = desc->v1.channel;
        device_timestamp = desc->v1.gp2_on_air_rise;
        rxi.rxi_tstamp = (uint32_t)le64toh(desc->v1.tsf_on_air_rise);
    }
    rxi.rxi_chan = chanidx;
    
    if (iwx_rx_reorder(sc, m, chanidx, desc,
                       (phy_info & IWX_RX_MPDU_PHY_SHORT_PREAMBLE),
                       rate_n_flags, device_timestamp, &rxi, ml))
        return;
    
    awdlCoexBaRxMbuf(sc, m, desc, 6);
    awdlRxMacMaskValid = sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210;
    awdlRxMacMask = awdlRxMacMaskValid ? desc->v3.mac_context : 0;
    iwx_rx_frame(sc, m, chanidx, le16toh(desc->status),
                 (phy_info & IWX_RX_MPDU_PHY_SHORT_PREAMBLE),
                 rate_n_flags, device_timestamp, &rxi, ml);
    awdlRxMacMaskValid = false;
}

void ItlIwx::
iwx_rx_frame(struct iwx_softc *sc, mbuf_t m, int chanidx,
             uint32_t rx_pkt_status, int is_shortpre, int rate_n_flags,
             uint32_t device_timestamp, struct ieee80211_rxinfo *rxi,
             struct mbuf_list *ml)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct ieee80211_frame *wh;
    struct ieee80211_node *ni;
    struct _ifnet *ifp = IC2IFP(ic);
    
    if (awdlCoexBaTraceEnabled)
        awdlCoexBaRxDiag.note(7, mtod(m, const uint8_t *), mbuf_len(m), false,
            awdlCoexAddr, awdlCoexAddrKnown, rx_pkt_status, 0, uint32_t(chanidx),
            awdlRxMacMaskValid ? awdlRxMacMask : 0, device_timestamp);

    // Whether the firmware's channel is real, before a stand-in replaces it.
    const bool chanValid = chanidx > 0 && chanidx < int(nitems(ic->ic_channels));
    if (chanidx < 0 || chanidx >= nitems(ic->ic_channels))
        chanidx = ieee80211_chan2ieee(ic, ic->ic_ibss_chan);
    if (awdlCoexProbe && awdlCoexAdded && awdlRxMacMaskValid)
        ++awdlCoexMacMaskSeen[awdlRxMacMask < 3 ? awdlRxMacMask : 3];
    
    if (awdlCoexProbe && awdlCoexAdded && mbuf_len(m) >= 24) {
        static const uint8_t awdlBssid[6] = {0x00, 0x25, 0x00, 0xff, 0x94, 0x73};
        const uint8_t *cf = mtod(m, const uint8_t *);
        const bool awdlFrame = !memcmp(cf + 16, awdlBssid, 6);
        // A Block Ack action frame to our AWDL address is ours whatever BSSID it carries.
        const bool baToUs = cf[0] == 0xd0 && mbuf_len(m) >= 26 && cf[24] == 3 && awdlCoexAddrKnown &&
            ItlAWDL::sameAddress(cf + 4, awdlCoexAddr);
        if (baToUs) {
            ++awdlCoexBaActionSeen[cf[25] < 3 ? cf[25] : 3];
            awdlCoexBaEvent('B', cf[25], awdlCoexBaPeerAdded && ItlAWDL::sameAddress(cf + 10, awdlCoexBaPeer) ? 1 : 0,
                            awdlFrame ? 1 : 0,
                            (chanValid && unsigned(chanidx) == awdlCoexChannel ? 1U : 0U) |
                            (awdlCoexPeerKnown(cf + 10, awdlUptimeUS()) ? 2U : 0U));
            ++(awdlFrame ? awdlCoexBaActionAwdlBssid : awdlCoexBaActionOtherBssid);
        }
        if (awdlCoexBaFilterOff && (cf[0] & 0x0c) == 0x08 && awdlFrame)
            ++awdlCoexRxWhileFilterOff;
        if ((awdlFrame || baToUs) && cf[0] == 0xd0) {
            ++awdlCoexAwdlAction;
            ++(unsigned(chanidx) == awdlCoexChannel ? awdlCoexActionOnChannel : awdlCoexActionOffChannel);
#if defined(ITL_AWDL_NATIVE)
            // The listener's frames come without the FCS; the hand-off expects four bytes after the
            // frame, as from monitor mode, so a copy gets them. Some setups also pad the frame with zeros
            // after its last TLV; that padding is cut, so the frame parses and is handed off as sent.
            size_t coexLen = mbuf_len(m);
            if (awdlCoexRx && !mbuf_next(m)) {
                const size_t unpadded = ItlAWDL::trimZeroPadding(cf, coexLen);
                if (unpadded != coexLen) {
                    coexLen = unpadded;
                    ++awdlCoexZeroPadTrimmed;
                }
            }
            ItlAWDL::ActionFrame coexAction;
            if (awdlCoexRx && !mbuf_next(m) &&
                ItlAWDL::parseAction(cf, coexLen, &coexAction) == ItlAWDL::ParseResult::Valid) {
                const uint64_t heardUS = awdlUptimeUS();
                const bool ourChannel = chanValid && unsigned(chanidx) == awdlCoexChannel;
                const bool fromOwnAddr = awdlCoexAddrKnown && ItlAWDL::sameAddress(cf + 10, awdlCoexAddr);
                if (!(cf[10] & 1) && ourChannel && !fromOwnAddr) {
                    awdlCoexPeerHeard(cf + 10, heardUS);
                    // The phone's schedule, from its sync frames, as dedicated mode tracks it: its BA peer
                    // once there is one, otherwise the first peer heard.
                    if ((!awdlCoexBaPeerAdded || ItlAWDL::sameAddress(cf + 10, awdlCoexBaPeer)) &&
                        (ItlAWDL::sameAddress(cf + 10, awdlCoexSchedPeer) || !awdlCoexScheduleCurrent() ||
                         !awdlCoexPeerSchedule.fresh(heardUS))) {
                        ItlAWDL::PeerSyncInfo peerSync{};
                        bool badSequence = false;
                        if (ItlAWDL::peerSyncFromAction(cf, coexLen, &peerSync, &badSequence)) {
                            if (!awdlCoexScheduleCurrent() || !ItlAWDL::sameAddress(cf + 10, awdlCoexSchedPeer)) {
                                awdlCoexPeerSchedule.reset();
                                awdlCoexSchedToken = awdlCoexToken;
                                memcpy(awdlCoexSchedPeer, cf + 10, sizeof(awdlCoexSchedPeer));
                            }
                            awdlCoexPeerSchedule.notePeerFrame(heardUS);
                            awdlCoexPeerSchedule.note(peerSync, heardUS, uint8_t(awdlCoexChannel));
                            ++awdlCoexPeerSyncFrames;
                        } else if (awdlCoexScheduleCurrent() && ItlAWDL::sameAddress(cf + 10, awdlCoexSchedPeer)) {
                            if (badSequence) {
                                ++awdlCoexPeerSequenceRejected;
                                awdlCoexPeerSchedule.invalidate();
                            } else {
                                awdlCoexPeerSchedule.notePeerFrame(heardUS);
                            }
                        }
                    }
                }
                // -itlawdlsvcmerge: the peer's other service records added, if any are missing (iface81).
                bool handed = false;
                if (awdlServiceMerge && !fromOwnAddr && ourChannel) {
                    if (awdlCoexServiceCacheToken != awdlCoexToken) {
                        awdlServiceCache.clear();
                        awdlCoexServiceCacheToken = awdlCoexToken;
                    }
                    awdlServiceCache.note(cf + 10, cf, coexLen);
                    unsigned added = 0;
                    mbuf_t merged = nullptr;
                    if (!mbuf_getpacket(MBUF_DONTWAIT, &merged) && merged && mbuf_maxlen(merged) > coexLen + 4) {
                        const size_t room = MIN(size_t(mbuf_maxlen(merged)) - 4, ItlAWDL::kServiceMergeFrameMax);
                        const size_t n = awdlServiceCache.merge(cf + 10, cf, coexLen,
                                                                static_cast<uint8_t *>(mbuf_data(merged)), room, &added);
                        if (n) {
                            memset(static_cast<uint8_t *>(mbuf_data(merged)) + n, 0, 4);
                            mbuf_setlen(merged, n + 4);
                            mbuf_pkthdr_setlen(merged, n + 4);
                            const IOReturn sent = itlAWDLReceiveAction(getController(), merged, n,
                                static_cast<uint32_t>(chanidx), rxi->rxi_rssi + IWX_MIN_DBM);
                            if (sent == kIOReturnSuccess) {
                                handed = true;
                                ++awdlCoexForwarded;
                                ++awdlServiceMergedFrames;
                                awdlServiceMergedRecords += added;
                            } else {
                                ++awdlServiceMergeRefused;
                                awdlServiceMergeLastError = uint32_t(sent);
                            }
                        }
                    }
                    if (merged)
                        mbuf_freem(merged);
                }
                mbuf_t copy = nullptr;
                if (!handed && !mbuf_getpacket(MBUF_DONTWAIT, &copy) && copy && mbuf_maxlen(copy) >= coexLen + 4) {
                    memcpy(mbuf_data(copy), cf, coexLen);
                    memset(static_cast<uint8_t *>(mbuf_data(copy)) + coexLen, 0, 4);
                    mbuf_setlen(copy, coexLen + 4);
                    mbuf_pkthdr_setlen(copy, coexLen + 4);
                    if (itlAWDLReceiveAction(getController(), copy, coexLen, static_cast<uint32_t>(chanidx),
                                             rxi->rxi_rssi + IWX_MIN_DBM) == kIOReturnSuccess)
                        ++awdlCoexForwarded;
                    else
                        ++awdlCoexForwardRefused;
                }
                if (copy)
                    mbuf_freem(copy);
            } else if (awdlCoexRx) {
                ++awdlCoexForwardMalformed;
                // Not AWDL's own format: a Block Ack request or teardown to our address, from a peer we hear.
                ItlAWDL::AddbaRequest request;
                bool delbaPeerWasSender = false;
                uint8_t delbaTid = 0;
                if (awdlCoexData && awdlCoexAddrKnown && !mbuf_next(m) && chanValid &&
                    unsigned(chanidx) == awdlCoexChannel && awdlCoexPeerKnown(cf + 10, awdlUptimeUS())) {
                    if (ItlAWDL::parseAddbaRequest(cf, coexLen, awdlCoexAddr, &request) && awdlFrame) {
                        ++awdlCoexBaRequests;
                        awdlCoexBaEvent('Q', request.token, request.tid, 0);
                        if (request.immediate) {
                            if ((awdlCoexBaPeerAdded && !ItlAWDL::sameAddress(request.peer, awdlCoexBaPeer)) ||
                                !awdlCoexBaRequestsPending.note(request)) {
                                ++awdlCoexBaOtherPeer;
                            } else if (awdlCoexBaAnswerLength && awdlCoexBaAnswerTid == request.tid &&
                                       awdlCoexBaAnswer[26] != request.token) {
                                awdlCoexBaAnswerLength = 0;
                            }
                        }
                    } else if (ItlAWDL::parseDelba(cf, coexLen, awdlCoexAddr, &delbaPeerWasSender, &delbaTid) &&
                               delbaTid < 8 && awdlCoexBaPeerAdded && ItlAWDL::sameAddress(cf + 10, awdlCoexBaPeer)) {
                        awdlCoexBaEvent('D', delbaTid, delbaPeerWasSender ? 1 : 0, awdlFrame ? 1 : 0);
                        if (!awdlFrame)
                            ++awdlCoexBaNonAwdlBssidAccepted;
                        if (delbaPeerWasSender) {
                            if (!awdlFrame && (awdlCoexBaTids & (1U << delbaTid)))
                                ++awdlCoexBaNonAwdlBssidApplied;
                            // A later request on this TID can be handled once removal finishes.
                            awdlCoexBaRequestsPending.discard(delbaTid);
                            if (awdlCoexBaAnswerLength && awdlCoexBaAnswerTid == delbaTid)
                                awdlCoexBaAnswerLength = 0;
                            awdlCoexBaRxIdleTids &= uint8_t(~(1U << delbaTid));
                            // Its session for sending to us: removed from the firmware on the next pass.
                            ++awdlCoexBaDelbas;
                            awdlCoexBaTidTeardown |= uint8_t(1U << delbaTid);
                        } else if (delbaTid == kAwdlBaTxTid &&
                                   (awdlCoexBaTxState == kCoexBaTxAgreed || awdlCoexBaTxState == kCoexBaTxEnabling)) {
                            // Our session for sending to it: back to the single-frame queue, and ask again.
                            if (!awdlFrame)
                                ++awdlCoexBaNonAwdlBssidApplied;
                            ++awdlCoexBaTxDelbas;
                            awdlCoexBaTxState = kCoexBaTxAsking;
                            ++awdlCoexBaTxToken;
                            awdlCoexBaProbeTried = false;
                            awdlCoexBaProbeUntilUS = 0;
                            awdlCoexBaTxAsks = 0;
                            awdlCoexBaTxLastAskUS = 0;
                        }
                    } else {
                        ItlAWDL::AddbaAnswer answer;
                        const bool parsed = ItlAWDL::parseAddbaAnswer(cf, coexLen, awdlCoexAddr, &answer);
                        const bool matches = parsed && awdlCoexBaTxState == kCoexBaTxAsking && awdlCoexBaTxAsks &&
                            awdlCoexBaPeerAdded && ItlAWDL::sameAddress(cf + 10, awdlCoexBaPeer) &&
                            answer.token == awdlCoexBaTxToken && answer.tid == kAwdlBaTxTid;
                        if (parsed)
                            awdlCoexBaEvent('R', answer.token, answer.status,
                                            (matches ? 1U : 0U) | (answer.immediate ? 2U : 0U) |
                                            (awdlFrame ? 4U : 0U), answer.window);
                        if (matches) {
                            if (!awdlFrame)
                                ++awdlCoexBaNonAwdlBssidAccepted;
                            if (!answer.status && !answer.immediate)
                                ++awdlCoexBaTxPolicyRefusals; // Delayed Block Ack isn't supported: treated as a refusal.
                            if (answer.status || !answer.immediate) {
                                ++awdlCoexBaTxRefusals;
                                awdlCoexBaTxRestUntilUS = awdlUptimeUS() + 20000000ULL;
                                ++awdlCoexBaTxToken;
                                awdlCoexBaProbeTried = false;
                                awdlCoexBaProbeUntilUS = 0;
                                awdlCoexBaTxAsks = 0;
                            } else {
                                if (!awdlFrame)
                                    ++awdlCoexBaNonAwdlBssidApplied;
                                ++awdlCoexBaTxAgreements;
                                // A window of 0 isn't valid in an answer; it's taken as our requested 64.
                                awdlCoexBaTxWindow = answer.window && answer.window < 64 ? answer.window : 64;
                                if (awdlCoexBaTxWindow < 64)
                                    ++awdlCoexBaTxSmallWindows;
                                awdlCoexBaTxState = kCoexBaTxEnabling;
                            }
                        }
                    }
                }
            }
#endif
        }
        else if (awdlFrame && (cf[0] & 0x0c) == 0x08) {
            ++awdlCoexAwdlData;
#if defined(ITL_AWDL_NATIVE)
            // AWDL data heard by the listener, without the FCS. awdl0's hand-off expects the monitor-mode
            // layout, four bytes after the frame, so it gets a copy with them zeroed.
            const size_t dataLen = mbuf_len(m);
            ++((cf[4] & 1) ? awdlCoexDataRxGroup : awdlCoexDataRxUcast);
            if (!(cf[4] & 1)) {
                if (cf[1] & 0x08)
                    ++awdlCoexDataRxRetry;
                ++awdlCoexRxModulation[(uint32_t(rate_n_flags) & IWX_RATE_MCS_MOD_TYPE_MSK) >> IWX_RATE_MCS_MOD_TYPE_POS];
                if ((uint32_t(rate_n_flags) & IWX_RATE_MCS_CHAN_WIDTH_MSK) == IWX_RATE_MCS_CHAN_WIDTH_80)
                    ++awdlCoexRxWide80;
                if (awdlVht80TwoStream(rate_n_flags))
                    ++awdlCoexRxVht80TwoStream;
            }
            if (awdlCoexData && !mbuf_next(m) && dataLen + 4 <= sizeof(awdlCoexDataBuf)) {
                ++awdlCoexDataRx;
                ItlAWDL::DataFrameView view;
                if (!chanValid || unsigned(chanidx) != awdlCoexChannel) {
                    ++awdlCoexDataRxOffChannel;
                } else if (!awdlCoexPeerKnown(cf + 10, awdlUptimeUS())) {
                    ++awdlCoexDataRxUnknownPeer;
                    awdlCoexNoteRejectReason(cf + 10, awdlUptimeUS());
                } else if (ItlAWDL::parseDataFrame(cf, dataLen, false, &view) == ItlAWDL::ParseResult::Valid) {
                    memcpy(awdlCoexDataBuf, cf, dataLen);
                    memset(awdlCoexDataBuf + dataLen, 0, 4);
                    if (awdlCoexBaPeerAdded && ItlAWDL::sameAddress(cf + 10, awdlCoexBaPeer)) {
                        awdlCoexBaPeerLastUS = awdlUptimeUS();
                        if (awdlCoexAddrKnown && ItlAWDL::sameAddress(view.destination, awdlCoexAddr))
                            (void)awdlCoexPeerKnown(cf + 10, awdlCoexBaPeerLastUS, true);
                        if (!(cf[4] & 1) && dataLen >= 1200)
                            awdlCoexBaLastRxPayloadUS = awdlCoexBaPeerLastUS;
                        if ((cf[0] & 0xf0) == 0x80 && dataLen >= 26) // QoS data: TID in the QoS control field.
                            awdlCoexBaRxTidLastUS[cf[24] & 7] = awdlCoexBaPeerLastUS;
                    }
                    itlAWDLObserveData(getController(), awdlCoexDataBuf, dataLen + 4);
                    if (awdlCoexScheduleCurrent() && ItlAWDL::sameAddress(cf + 10, awdlCoexSchedPeer)) {
                        awdlCoexSchedPeerRxUS = awdlUptimeUS();
                        awdlCoexPeerSchedule.notePeerFrame(awdlCoexSchedPeerRxUS);
                    }
                    ++awdlCoexDataRxDelivered;
                    awdlCoexLastDataUS = awdlUptimeUS();
                } else {
                    ++awdlCoexDataRxMalformed;
                }
            }
#endif
        }
        else
            ++awdlCoexOther;
        if (++awdlCoexPublished % 512 == 0)
            awdlCoexPublish();
        // Frames with AWDL's BSSID are never the station's, whatever the filter: they stop at the hand-off.
        if ((awdlFrame || baToUs) && awdlCoexRx) {
            mbuf_freem(m);
            return;
        }
    }
    // With promiscuous receive (or a filter state the firmware may hold), what the listener hears goes no
    // further than the AWDL hand-off above, whatever its length. A frame is the listener's if the firmware's
    // MAC mask names the listener and not the station, or if it came in on the listener's channel while the
    // station is on another. A frame with neither usable is counted and left to the station.
    if (awdlCoexProbe && (awdlCoexPromiscLive || awdlCoexPromiscUncertain)) {
        struct iwx_node *stationNode = (struct iwx_node *)ic->ic_bss;
        const unsigned stationChannel = stationNode && stationNode->in_phyctxt && stationNode->in_phyctxt->channel ?
            unsigned(ieee80211_chan2ieee(ic, stationNode->in_phyctxt->channel)) : 0;
        const bool maskListenerOnly = awdlRxMacMaskValid && (awdlRxMacMask & (1U << kAwdlCoexMacId)) &&
            !(awdlRxMacMask & 1U);
        const bool channelListener = chanValid && stationChannel && unsigned(chanidx) == awdlCoexChannel &&
            stationChannel != awdlCoexChannel;
        if (maskListenerOnly || channelListener) {
            ++awdlCoexPromiscDropped;
            mbuf_freem(m);
            return;
        }
        // A present but zero mask names no context (iface132: always 0), so it doesn't count as evidence.
        if (!chanValid && !(awdlRxMacMaskValid && awdlRxMacMask))
            ++awdlCoexPromiscAmbiguous;
    }

    if (awdlRx) {
        awdlRxObserve(sc, m, chanidx, rxi->rxi_rssi, device_timestamp);
        const uint8_t *frameBytes = mtod(m, const uint8_t *);
        if (mbuf_len(m) >= 24 && (frameBytes[0] & 0x0c) == 8 && ItlAWDL::knownAddress(awdlOwnAddress) &&
            ItlAWDL::sameAddress(frameBytes + 4, awdlOwnAddress))
        {
            if (awdlTx80 && awdlWide80Live && chanidx == 149 &&
                (rate_n_flags & IWX_RATE_MCS_MOD_TYPE_MSK) == IWX_RATE_MCS_VHT_MSK &&
                (rate_n_flags & IWX_RATE_MCS_CHAN_WIDTH_MSK) == IWX_RATE_MCS_CHAN_WIDTH_80 &&
                ItlAWDL::knownAddress(awdlLastPeer) && ItlAWDL::sameAddress(frameBytes + 10, awdlLastPeer)) {
                memcpy(awdlTx80Peer, frameBytes + 10, sizeof(awdlTx80Peer));
                awdlTx80PeerUS = awdlUptimeUS();
                awdlTx80PeerGeneration = sc->sc_generation;
            }
            ItlAWDL::noteRxRate(awdlRxRates, nitems(awdlRxRates), uint32_t(rate_n_flags), awdlRxRatesOverflow);
            if (awdlRxPhyInfo & IWX_RX_MPDU_PHY_AMPDU)
                ++awdlBaRxAmpdu;
        }
    }
    wh = mtod(m, struct ieee80211_frame *);
    ni = ieee80211_find_rxnode(ic, wh);
    if ((rxi->rxi_flags & IEEE80211_RXI_HWDEC) &&
        iwx_ccmp_decap(sc, m, ni, rxi) != 0) {
        ifp->netStat->inputErrors++;
        mbuf_freem(m);
        ieee80211_release_node(ic, ni);
        return;
    }
    
#if NBPFILTER > 0
    if (sc->sc_drvbpf != NULL) {
        struct iwx_rx_radiotap_header *tap = &sc->sc_rxtap;
        uint16_t chan_flags;
        
        tap->wr_flags = 0;
        if (is_shortpre)
            tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
        tap->wr_chan_freq =
        htole16(ic->ic_channels[chanidx].ic_freq);
        chan_flags = ic->ic_channels[chanidx].ic_flags;
        if (ic->ic_curmode != IEEE80211_MODE_11N)
            chan_flags &= ~IEEE80211_CHAN_HT;
        tap->wr_chan_flags = htole16(chan_flags);
        tap->wr_dbm_antsignal = (int8_t)rxi->rxi_rssi;
        tap->wr_dbm_antnoise = (int8_t)sc->sc_noise;
        tap->wr_tsft = device_timestamp;
        if (rate_n_flags & IWX_RATE_MCS_HT_MSK) {
            uint8_t mcs = (rate_n_flags &
                           (IWX_RATE_HT_MCS_RATE_CODE_MSK |
                            IWX_RATE_HT_MCS_NSS_MSK));
            tap->wr_rate = (0x80 | mcs);
        } else {
            uint8_t rate = (rate_n_flags &
                            IWX_RATE_LEGACY_RATE_MSK);
            switch (rate) {
                    /* CCK rates. */
                case  10: tap->wr_rate =   2; break;
                case  20: tap->wr_rate =   4; break;
                case  55: tap->wr_rate =  11; break;
                case 110: tap->wr_rate =  22; break;
                    /* OFDM rates. */
                case 0xd: tap->wr_rate =  12; break;
                case 0xf: tap->wr_rate =  18; break;
                case 0x5: tap->wr_rate =  24; break;
                case 0x7: tap->wr_rate =  36; break;
                case 0x9: tap->wr_rate =  48; break;
                case 0xb: tap->wr_rate =  72; break;
                case 0x1: tap->wr_rate =  96; break;
                case 0x3: tap->wr_rate = 108; break;
                    /* Unknown rate: should not happen. */
                default:  tap->wr_rate =   0;
            }
        }
        
        bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_rxtap_len,
                     m, BPF_DIRECTION_IN);
    }
#endif
    ieee80211_inputm(IC2IFP(ic), m, ni, rxi, ml);
    ieee80211_release_node(ic, ni);
}

void ItlIwx::
