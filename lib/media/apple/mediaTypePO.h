#ifndef _MEDIATYPE_PO_
#define _MEDIATYPE_PO_

#include <stdio.h>

#include "mediaType.h"

class MediaTypePO : public MediaType
{
private:
    uint32_t last_block_num = 0xFFFFFFFF;
public:
    virtual bool read(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override;
    virtual bool write(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override;

    virtual bool format(uint16_t *respopnsesize) override;

    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;

    virtual bool status() override {return (_media_fileh != nullptr);}

    // static bool create(FILE *f, uint32_t numBlock);

    size_t size() {return _media_num_sectors;}
    void reset_seek_opto() {last_block_num = 0xFFFFFFFF;};
};


#endif // _MEDIATYPE_PO_
