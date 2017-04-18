//
// Created by koncord on 16.04.17.
//

#ifndef OPENMW_PROCESSORPLAYERCELLCHANGE_HPP
#define OPENMW_PROCESSORPLAYERCELLCHANGE_HPP


#include "apps/openmw/mwmp/PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerCellChange : public PlayerProcessor
    {
    public:
        ProcessorPlayerCellChange()
        {
            BPP_INIT(ID_PLAYER_CELL_CHANGE)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            if (isLocal())
            {
                if (isRequest())
                    static_cast<LocalPlayer *>(player)->updateCell(true);
                else
                    static_cast<LocalPlayer *>(player)->setCell();
            }
            else
                static_cast<DedicatedPlayer*>(player)->updateCell();
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERCELLCHANGE_HPP