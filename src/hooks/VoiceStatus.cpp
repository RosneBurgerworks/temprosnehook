// hutchyben did this for cowhook and he needs the code in to be compilable for cathook (he doesnt know how to hook methods)
#include "HookedMethods.hpp"
#if ENABLE_VISUALS
#endif

namespace hooked_methods
{

DEFINE_HOOKED_METHOD(VoiceStatus, void, void *this_, int entindex, qboolean bTalking )
{
   if (entindex == g_pLocalPlayer->entity_idx || entindex == -2) {
       original::VoiceStatus(this_, entindex, true);
       return;
   }

   original::VoiceStatus(this_, entindex, bTalking);
}
} // namespace