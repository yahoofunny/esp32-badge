#include "clawd_album.h"
#include "assets_clawd_album.h"

namespace clawd_album {
int count() { return sizeof(ALBUM)/sizeof(ALBUM[0]); }
const Entry *at(int index) { return index>=0 && index<count()?&ALBUM[index]:nullptr; }
}
