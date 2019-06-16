#pragma once

#include <ion/ion.hpp>
#include <ion/print.hpp>
#include <ion/asset.hpp>

#include "../core/commission.hpp"
#include "../core/contract.hpp"
#include "../core/dice.hpp"
#include "../core/string_format.hpp"

using namespace plasma;

//--------------------------------------------------------------------------------

class [[ion::contract]] plasma_dice : public ion::contract, public plasma::commission
{
public:
    plasma_dice(ion::name receiver, ion::name code, ion::datastream<const char*> ds);
    virtual ~plasma_dice() {}

    [[ion::action]]
    void bet(ion::name user, std::string quantity, uint8_t roll_under, ion::checksum256 seed_hash, ion::checksum256 user_seed_hash, bool paySysCms);

    [[ion::action]]
    void dice(ion::name user, uint64_t bet_id, ion::checksum256 seed);

    [[ion::action]]
    void ssh(ion::name user, std::string str);

private:
    void assert_quantity(const plasma::big_asset& _quantity);
    void assert_roll_under(const uint8_t& roll_under, const plasma::big_asset& _quantity);
    void assert_hash(const ion::checksum256& seed_hash);
    void assert_seed(const ion::checksum256& seed, const ion::checksum256& hash);

    uint64_t next_id();
    uint8_t compute_random_roll(const ion::checksum256& seed1, const ion::checksum256& seed2);

    plasma::big_asset compute_payout(uint8_t roll_under, const plasma::big_asset& offer);
    plasma::big_asset compute_comission(const plasma::big_asset & payout);
    plasma::big_asset compute_available_balance(const plasma::big_asset& quantity);

    std::string winner_memo(const plasma::allbets& bet);
    std::string commission_memo(const plasma::allbets& bet);

    void dicepool_lock(const plasma::big_asset& amount);
    void dicepool_unlock(const plasma::big_asset& amount);

private:
    TABLE bet_ : public plasma::allbets {};
    using TablePlasmaAllBets_t = ion::multi_index<"allbets"_n, bet_>;
    TablePlasmaAllBets_t bets_;

    TABLE pool_ : public plasma::dicepool {};
    using TablePlasmaDicePool_t = ion::multi_index<"dicepool"_n, pool_>;
    TablePlasmaDicePool_t dicepool_;

    // TABLE ident_ : public plasma::identity {};
    // using TablePlasmaIdentity_t = ion::singleton<"identity"_n, ident_>;
    // TablePlasmaIdentity_t identity_;
    plasma::TablePlasmaIdentity identity_;
};

//--------------------------------------------------------------------------------
