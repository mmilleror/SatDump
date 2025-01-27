#include "mwts3_reader.h"
#include "common/ccsds/ccsds_time.h"

namespace fengyun3
{
    namespace mwts3
    {
        MWTS3Reader::MWTS3Reader()
        {
            for (int i = 0; i < 18; i++)
                channels[i].resize(98);

            lines = 0;
        }

        MWTS3Reader::~MWTS3Reader()
        {
            for (int i = 0; i < 18; i++)
                channels[i].clear();
        }

        void MWTS3Reader::work(ccsds::CCSDSPacket &packet)
        {
            if (packet.payload.size() < 1018)
                return;

            int marker = (packet.payload[0] >> 4) & 0b111;

            if (marker == 1)
            {
                int pos = 224 + 144 * 2;
                for (int i = 0; i < 14; i++)
                {
                    for (int c = 0; c < 18; c++)
                        channels[c][lines * 98 + i] = packet.payload[pos + (18 * i + c) * 2 + 0] << 8 | packet.payload[pos + (18 * i + c) * 2 + 1];
                }

                double currentTime = ccsds::parseCCSDSTimeFullRaw(&packet.payload[2], 10957, 10000, 10000) + 12 * 3600;
                timestamps.push_back(currentTime);
                lines++;
            }
            else if (marker == 2)
            {
                int pos = 8;
                for (int i = 0; i < 28; i++)
                {
                    for (int c = 0; c < 18; c++)
                        channels[c][lines * 98 + 14 + i] = packet.payload[pos + (18 * i + c) * 2 + 0] << 8 | packet.payload[pos + (18 * i + c) * 2 + 1];
                }
            }
            else if (marker == 3)
            {
                int pos = 8;
                for (int i = 0; i < 28; i++)
                {
                    for (int c = 0; c < 18; c++)
                        channels[c][lines * 98 + 42 + i] = packet.payload[pos + (18 * i + c) * 2 + 0] << 8 | packet.payload[pos + (18 * i + c) * 2 + 1];
                }
            }
            else if (marker == 4)
            {
                int pos = 8;
                for (int i = 0; i < 28; i++)
                {
                    for (int c = 0; c < 18; c++)
                        channels[c][lines * 98 + 70 + i] = packet.payload[pos + (18 * i + c) * 2 + 0] << 8 | packet.payload[pos + (18 * i + c) * 2 + 1];
                }
            }

            // Make sure we have enough room
            for (int i = 0; i < 18; i++)
                channels[i].resize((lines + 1) * 98);
        }

        image::Image<uint16_t> MWTS3Reader::getChannel(int channel)
        {
            return image::Image<uint16_t>(channels[channel].data(), 98, lines, 1);
        }
    }
}