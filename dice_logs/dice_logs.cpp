
#include <ion/ion.hpp>

#include "../../core/dice.hpp"

//--------------------------------------------------------------------------------

class [[ion::contract]] dice_logs : public ion::contract
{
public:
    dice_logs(ion::name receiver, ion::name code, ion::datastream<const char*> ds) : contract(receiver, code, ds) {}

    [[ion::action]]
    void receipt(plasma::allbets bet)
    {
        // require_auth(_self);
        require_recipient(bet.player);
    }

    [[ion::action]]
    void prize(plasma::bets_result result)
    {
        // require_auth(_self);
        require_recipient(result.player);
    }
};

//--------------------------------------------------------------------------------

ION_DISPATCH(dice_logs, (receipt)(prize))
