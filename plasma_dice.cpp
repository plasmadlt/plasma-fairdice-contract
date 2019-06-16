#include "plasma_dice.hpp"

#include <algorithm>

#include <ion/crypto.hpp>
#include <ion/system.hpp>

#include "../core/accounts.hpp"
#include "../core/token_types64.hpp"

//--------------------------------------------------------------------------------

plasma_dice::plasma_dice(ion::name receiver, ion::name code, ion::datastream<const char*> ds)
  : contract(receiver, code, ds)
  , bets_(receiver, code.value)
  , dicepool_(receiver, code.value)
  , identity_(receiver, code.value)
{
}

void plasma_dice::bet(ion::name user, std::string quantity, uint8_t roll_under, 
                      ion::checksum256 seed_hash, ion::checksum256 user_seed_hash, bool paySysCms)
{
    require_auth(user);
    ion::internal_use_do_not_use::require_auth2(user.value, plasma::accountPlasmaDicePermission.value);

    plasma::big_asset _quantity = plasma::big_asset::from_string(quantity);

    assert_quantity(_quantity);
    assert_roll_under(roll_under, _quantity);

    //check seed hash && expiration
    // assert_hash(seed_hash, expiration);

    uint64_t key = next_id();
    auto it = bets_.find(key);

    ion::print(", bet ID [#", key,"]");
    ion::check(it == bets_.end(), "duplicate bet ID");
    ion::check(_quantity.symbol.code() != plasma::system_symbol, "only stable coins are supported!");

    //--------------------------------------------------------------------------------
    //  transfer to fund pool

    if (_quantity.symbol.code() == plasma::system_symbol)
    {
        ion::action(
            ion::permission_level{ user, ion::name("active") },
            plasma::accountIonToken,
            ion::name("transfer"),
            std::make_tuple(user, plasma::accountPlasmaDiceBank, quantity, std::string(""))
        ).send();
    }
    else
    {
        std::string issuer(_quantity.symbol.code().to_string());
        std::transform(issuer.begin(), issuer.end(), issuer.begin(), ::tolower);

        ion::action(
            ion::permission_level{ user, ion::name("active") },
            ion::name(issuer),
            ion::name("transfer"),
            std::make_tuple(user, plasma::accountPlasmaDiceBank, plasma::to_string(_quantity.get_amount()), paySysCms, std::string("bet placed"))
        ).send();

        auto commission = get_commission(paySysCms);
        _quantity = _quantity - (_quantity * commission.first) / (commission.second * 100);
    }

    //--------------------------------------------------------------------------------
    //  save bet

    plasma::allbets new_bet = { key, user, _quantity, roll_under, seed_hash, user_seed_hash, ion::current_time_point() };
    bets_.emplace(user, [&](auto& r) {
        r.id = new_bet.id;
        r.player = new_bet.player;
        r.amount = new_bet.amount;
        r.roll_under = new_bet.roll_under;
        r.seed_hash = new_bet.seed_hash;
        r.user_seed_hash = new_bet.user_seed_hash;
        r.created_at = new_bet.created_at;
    });

    //--------------------------------------------------------------------------------
    // lock deposit

    dicepool_lock(new_bet.amount);

    //--------------------------------------------------------------------------------
    //  display receipt

    ion::action(
        ion::permission_level{ _self, ion::name("active") },
        ion::name(plasma::accountPlasmaDiceLogs),
        ion::name("receipt"),
        std::make_tuple(new_bet)
    ).send();
}

void plasma_dice::dice(ion::name user, uint64_t id, ion::checksum256 seed)
{
    require_auth(user);
    ion::internal_use_do_not_use::require_auth2(user.value, plasma::accountPlasmaDicePermission.value);

    auto it = bets_.find(id);

    ion::check(it != bets_.end(), "bet not found");
    ion::check(it->amount.symbol.code() != plasma::system_symbol, "only stable coins are supported!");

    //--------------------------------------------------------------------------------
    //  random roll generation

    // assert_seed(seed, it->seed_hash);
    uint8_t random_roll = compute_random_roll(seed, it->user_seed_hash);

    plasma::big_asset payout = plasma::big_asset(0, it->amount.symbol);
    if (random_roll < it->roll_under)
    {
        payout = compute_payout(it->roll_under, it->amount);
        plasma::big_asset commission = compute_comission(payout);

        ion::print("commission [", commission,"], ");
        ion::print("payout [", payout - commission, "]");

        //--------------------------------------------------------------------------------
        //  commission processing

        plasma::token::transfer(plasma::accountPlasmaDiceBank, plasma::accountPlasmaDice, commission, commission_memo(*it));

        //--------------------------------------------------------------------------------
        //  payouts processing

        if (it->amount.symbol.code() == plasma::system_symbol)
        {
            ion::action(
                ion::permission_level{ plasma::accountPlasmaDiceBank, ion::name("active") },
                plasma::accountIonToken,
                ion::name("transfer"),
                std::make_tuple(plasma::accountPlasmaDiceBank, user, payout - commission, winner_memo(*it))
            ).send();
        }
        else
        {
            std::string issuer(it->amount.symbol.code().to_string());
            std::transform(issuer.begin(), issuer.end(), issuer.begin(), ::tolower);

            auto transfer_amount = plasma::to_string(payout.get_amount() - commission.get_amount());
            ion::action(
                ion::permission_level{ plasma::accountPlasmaDiceBank, ion::name("active") },
                ion::name(issuer),
                ion::name("transfer"),
                std::make_tuple(plasma::accountPlasmaDiceBank, user, transfer_amount, bool(false), winner_memo(*it))
            ).send();
        }

        //--------------------------------------------------------------------------------
        // unlock deposit

        dicepool_unlock(it->amount);
    }

    //--------------------------------------------------------------------------------
    //  dice result

    plasma::bets_result win = {
        it->id, it->player, it->amount, it->roll_under, random_roll, seed, it->seed_hash, it->user_seed_hash, payout
    };

    ion::action(
        ion::permission_level{ _self, ion::name("active") },
        ion::name(plasma::accountPlasmaDiceLogs),
        ion::name("prize"),
        std::make_tuple(win)
    ).send();

    //--------------------------------------------------------------------------------
    //  remove bet

    bets_.erase(it);
}

//--------------------------------------------------------------------------------

plasma::big_asset plasma_dice::compute_payout(uint8_t roll_under, const plasma::big_asset& offer)
{
    const double ODDS = 98.0 / ((double)roll_under - 1.0);
    return plasma::big_asset(ODDS * offer.get_amount(), offer.symbol);
    
    //return offer * ODDS;
    // return std::min(ion::asset(ODDS * offer.amount, offer.symbol), compute_available_balance(offer));
}

plasma::big_asset plasma_dice::compute_comission(const plasma::big_asset & payout)
{
    return payout / (10 * 100) * 2;
}

plasma::big_asset plasma_dice::compute_available_balance(const plasma::big_asset& _quantity)
{
    plasma::big_asset locked(0, _quantity.symbol);
    auto it = dicepool_.find(_quantity.symbol.raw());
    if (it != dicepool_.end())
    {
        locked = it->locked;
    }

    plasma::big_asset balance = plasma::tokenTypes::getBalanceEx(plasma::accountPlasmaDiceBank, _quantity.symbol.code());
    ion::print("pool [", balance, "], locked [", locked, "]");

    plasma::big_asset available = balance - locked;
    ion::check(available.get_amount() >= 0, "fund pool overdraw");

    return available;
}

uint8_t plasma_dice::compute_random_roll(const ion::checksum256& seed1, const ion::checksum256& seed2)
{
    size_t hash = 0;
    plasma::hash_combine(hash, plasma::sha256_to_hex(seed1));
    plasma::hash_combine(hash, plasma::sha256_to_hex(seed2));
    return hash % 100 + 1;
}

uint64_t plasma_dice::next_id()
{
    plasma::identity tmp = identity_.get_or_default();
    tmp.current_id++;

    identity_.set(tmp, _self);
    return tmp.current_id;
}

void plasma_dice::assert_quantity(const plasma::big_asset& _quantity)
{
    ion::check(_quantity.is_valid(), "quantity invalid");
    // ion_assert(quantity.symbol != plasma::system_symbol, "only stable coins allowed");
    // ion_assert(quantity.amount >= 1000, "transfer quantity must be greater than 0.1");
}

void plasma_dice::assert_roll_under(const uint8_t& roll_under, const plasma::big_asset& _quantity)
{
    ion::check(roll_under >= 2 && roll_under <= 96,
        "roll under overflow, must be in range [2 : 96]");

    ion::check(compute_payout(roll_under, _quantity) <= compute_available_balance(_quantity),
        "expected payout is greater than the maximum bonus");
}

void plasma_dice::assert_hash(const ion::checksum256&)
{
}

void plasma_dice::assert_seed(const ion::checksum256& seed, const ion::checksum256& hash)
{
    std::string seed_str = plasma::sha256_to_hex(seed);
    ion::assert_sha256(seed_str.c_str(), seed_str.length(), hash);
}

std::string plasma_dice::winner_memo(const plasma::allbets& bet)
{
    return "winner - bet id:" + std::to_string(bet.id) + ", player: " + bet.player.to_string();
}

std::string plasma_dice::commission_memo(const plasma::allbets& bet)
{
    return "commission - bet id:" + std::to_string(bet.id) + ", player: " + bet.player.to_string();
}

void plasma_dice::dicepool_lock(const plasma::big_asset& amount)
{
    auto it = dicepool_.find(amount.symbol.raw());
    if (it == dicepool_.end())
    {
        dicepool_.emplace(_self, [&](auto& new_locked) {
            new_locked.locked = amount;
        });
    }
    else
    {
        dicepool_.modify(it, _self, [&](auto& locked){
            locked.locked += amount;
        });
    }
}

void plasma_dice::dicepool_unlock(const plasma::big_asset& amount)
{
    auto it = dicepool_.find(amount.symbol.raw());
    ion::check(it != dicepool_.end(), "fund unlock error - bet not found");

    dicepool_.modify(it, _self, [&](auto& locked){
        locked.locked -= amount;
       ion::check(locked.locked.get_amount() >= 0, "fund unlock error - deposit overdraw");
    });
}

//--------------------------------------------------------------------------------

void plasma_dice::ssh(ion::name, std::string str)
{
    ion::checksum256 hash = ion::sha256(str.c_str(), str.length());
    ion::print(hash);
    assert_sha256(str.c_str(), str.length(), hash);
}

//--------------------------------------------------------------------------------

ION_DISPATCH(plasma_dice, (bet)(dice)(ssh))
