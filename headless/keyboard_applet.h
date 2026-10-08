// SPDX-License-Identifier: GPL-3.0-or-later
// A game's text entry (a character's name, an answer during an event) on the PS5's own on-screen
// keyboard (system_keyboard.h). Eden's built-in one answered every request with the same text.
//
// The keyboard is asked for on a thread of its own, since the player takes as long as they like
// and the game's request must not hold up the emulator. What the player enters goes back through
// the callbacks Eden gave; a request that was closed meanwhile (the game left the screen, the
// session ended) gets no answer.
//
// When the keyboard cannot be opened the game gets the text it started from, or "Eden", as
// before, so no game waits for good on a keyboard that never comes.
#pragma once
#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include "core/frontend/applets/software_keyboard.h"
#include "diagnostics.h"
#include "system_keyboard.h"

namespace Eden {
class SystemKeyboardApplet final : public Core::Frontend::SoftwareKeyboardApplet {
public:
    using Ask = std::function<TextAnswer(const TextRequest&, const std::atomic<bool>&)>;
    using Result = Service::AM::Frontend::SwkbdResult;
    using Reply = Service::AM::Frontend::SwkbdReplyType;
    using Check = Service::AM::Frontend::SwkbdTextCheckResult;
    // How often a text the game refuses, or one shorter than it asks for, is asked for again.
    static constexpr int kAttempts = 3;

    explicit SystemKeyboardApplet(Ask ask_) : state{std::make_shared<State>()} { state->ask = std::move(ask_); }
    ~SystemKeyboardApplet() override { End(); }

    void Close() const override { End(); }
    void ExitKeyboard() const override { End(); }
    void HideInlineKeyboard() const override { End(); }

    void InitializeKeyboard(bool is_inline, Core::Frontend::KeyboardInitializeParameters parameters,
                            SubmitNormalCallback normal, SubmitInlineCallback inline_) override {
        const std::lock_guard lock(state->mutex);
        state->request = {};
        state->request.title = !parameters.header_text.empty() ? parameters.header_text :
                               !parameters.sub_text.empty() ? parameters.sub_text : parameters.guide_text;
        state->request.placeholder = parameters.guide_text;
        state->request.initial = parameters.initial_text;
        state->request.max_length = parameters.max_text_length;
        state->request.numbers = parameters.type == Service::AM::Frontend::SwkbdType::NumberPad;
        state->request.password = parameters.password_mode == Service::AM::Frontend::SwkbdPasswordMode::Enabled;
        state->min_length = parameters.min_text_length;
        state->must_answer = parameters.disable_cancel_button;
        state->text = parameters.initial_text;
        state->refusals = 0;
        if (is_inline) state->submit_inline = std::move(inline_);
        else state->submit_normal = std::move(normal);
    }

    void ShowNormalKeyboard() const override {
        Start([](const std::shared_ptr<State>& s, std::uint64_t run, const std::atomic<bool>& stop) {
            Result result = Result::Cancel;
            std::u16string text;
            bool entered = false;
            for (int attempt = 0; attempt < kAttempts; ++attempt) {
                TextRequest request;
                {
                    const std::lock_guard lock(s->mutex);
                    request = s->request;
                    request.initial = s->text;
                }
                const TextAnswer answer = s->ask(request, stop);
                const std::lock_guard lock(s->mutex);
                if (s->run != run) return;  // closed meanwhile
                if (answer.outcome == TextOutcome::unavailable) {
                    result = Result::Ok;
                    text = Fallback(*s);
                    break;
                }
                if (answer.outcome == TextOutcome::cancelled) {
                    if (!s->must_answer) break;
                    continue;  // the game has no way out of this text: ask again
                }
                s->text = answer.text;
                if (answer.text.size() >= s->min_length) {
                    result = Result::Ok;
                    text = answer.text;
                    entered = true;
                    break;
                }
            }
            const std::lock_guard lock(s->mutex);
            if (s->run != run || !s->submit_normal) return;
            if (result == Result::Cancel && s->must_answer) {
                result = Result::Ok;
                text = Fallback(*s);
            }
            // A text the player entered is the game's to check; anything else is final.
            s->submit_normal(result, std::move(text), !entered);
        });
    }

    // The game's answer to a text it was given to check: refused (asked for again, a few times)
    // or to be confirmed (taken as confirmed: there is no dialog to ask with).
    void ShowTextCheckDialog(Check check, std::u16string message) const override {
        bool again = false;
        {
            const std::lock_guard lock(state->mutex);
            if (check == Check::Failure && ++state->refusals < kAttempts) {
                again = true;
            } else if (state->submit_normal) {
                const bool refused = check == Check::Failure;
                Report("keyboard", refused ? "The game refused the text several times: the entry is cancelled" :
                                             "The game asked to confirm the text: confirmed");
                auto submit = state->submit_normal;
                submit(refused ? Result::Cancel : Result::Ok, refused ? std::u16string{} : state->text, true);
            }
        }
        if (again) {
            Report("keyboard", "The game refused the text: asking again");
            ShowNormalKeyboard();
        }
    }

    // A keyboard the game shows inside its own screen: the text is entered on the PS5 keyboard
    // and handed to the game when it closes.
    void ShowInlineKeyboard(Core::Frontend::InlineAppearParameters appear) const override {
        {
            const std::lock_guard lock(state->mutex);
            state->request.max_length = appear.max_text_length;
            state->request.numbers = appear.type == Service::AM::Frontend::SwkbdType::NumberPad;
        }
        Start([](const std::shared_ptr<State>& s, std::uint64_t run, const std::atomic<bool>& stop) {
            TextRequest request;
            {
                const std::lock_guard lock(s->mutex);
                request = s->request;
                request.initial = s->text;
            }
            const TextAnswer answer = s->ask(request, stop);
            const std::lock_guard lock(s->mutex);
            if (s->run != run || !s->submit_inline) return;
            if (answer.outcome == TextOutcome::cancelled) {
                s->submit_inline(Reply::DecidedCancel, s->text, static_cast<s32>(s->text.size()));
                return;
            }
            const std::u16string text = answer.outcome == TextOutcome::accepted ? answer.text : Fallback(*s);
            s->text = text;
            s->submit_inline(Reply::ChangedString, text, static_cast<s32>(text.size()));
            s->submit_inline(Reply::DecidedEnter, text, static_cast<s32>(text.size()));
        });
    }

    void InlineTextChanged(Core::Frontend::InlineTextParameters parameters) const override {
        const std::lock_guard lock(state->mutex);
        state->text = parameters.input_text;
        if (state->submit_inline)
            state->submit_inline(Reply::ChangedString, parameters.input_text, parameters.cursor_position);
    }

private:
    struct State {
        // Recursive: Eden closes the keyboard from inside the callback that hands it the text.
        std::recursive_mutex mutex;
        Ask ask;
        TextRequest request;
        std::u16string text;         // the text so far
        std::size_t min_length = 0;
        bool must_answer = false;    // the game shows no cancel button
        int refusals = 0;
        std::uint64_t run = 0;       // which request is open; a closed one is another number
        std::shared_ptr<std::atomic<bool>> stop;
        SubmitNormalCallback submit_normal;
        SubmitInlineCallback submit_inline;
    };

    // What the game gets without a keyboard: its own starting text when that is long enough,
    // else "Eden", cut and filled to the lengths it asked for.
    static std::u16string Fallback(const State& s) {
        std::u16string text = s.request.initial.size() >= std::max<std::size_t>(s.min_length, 1) ?
                                  s.request.initial : std::u16string{u"Eden"};
        if (s.request.max_length > 0 && text.size() > s.request.max_length) text.resize(s.request.max_length);
        if (text.size() < s.min_length) text.resize(s.min_length, u'0');
        return text;
    }

    // Closes what is open: its keyboard goes away and its answer, if one still comes, is dropped.
    void End() const {
        const std::lock_guard lock(state->mutex);
        ++state->run;
        if (state->stop) state->stop->store(true);
        state->stop.reset();
    }

    template <class Work>
    void Start(Work work) const {
        std::shared_ptr<std::atomic<bool>> stop;
        std::uint64_t run;
        {
            const std::lock_guard lock(state->mutex);
            run = ++state->run;
            if (state->stop) state->stop->store(true);
            stop = state->stop = std::make_shared<std::atomic<bool>>(false);
        }
        std::thread([s = state, run, stop, work] { work(s, run, *stop); }).detach();
    }

    std::shared_ptr<State> state;
};
}  // namespace Eden
