ItlAWDL::parseAddbaRequest(cf, coexLen, awdlCoexAddr, &request) && awdlFrame) {
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
                    } else if (ItlAWDL::parseDelba
