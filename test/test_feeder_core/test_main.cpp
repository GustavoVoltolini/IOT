// Testes da lógica pura. Rodam no PC: `pio test -e native`.
#include <unity.h>

#include "feeder_core.h"

using namespace feeder;

void setUp() {}
void tearDown() {}

// --- decideFeed -------------------------------------------------------------

void test_alimenta_pote_vazio() {
  const FeedDecision d = decideFeed(0, 30, 150, 10);
  TEST_ASSERT_TRUE(d.allowed);
  TEST_ASSERT_EQUAL_FLOAT(30, d.targetG);
}

void test_recusa_pote_no_limite() {
  // capacidade 150 - margem 10 = 140
  const FeedDecision d = decideFeed(140, 30, 150, 10);
  TEST_ASSERT_FALSE(d.allowed);
  TEST_ASSERT_EQUAL(Refusal::BowlFull, d.refusal);
}

void test_alvo_nunca_passa_da_capacidade() {
  const FeedDecision d = decideFeed(130, 50, 150, 10);
  TEST_ASSERT_TRUE(d.allowed);
  TEST_ASSERT_EQUAL_FLOAT(150, d.targetG);
}

void test_recusa_quantidade_invalida() {
  TEST_ASSERT_EQUAL(Refusal::InvalidAmount, decideFeed(0, 0, 150, 10).refusal);
  TEST_ASSERT_EQUAL(Refusal::InvalidAmount, decideFeed(0, -5, 150, 10).refusal);
}

void test_recusa_quando_cabe_muito_pouco() {
  // margem zero: 149 < 150, mas só cabe 1 g
  const FeedDecision d = decideFeed(149, 30, 150, 0);
  TEST_ASSERT_FALSE(d.allowed);
  TEST_ASSERT_EQUAL(Refusal::TooLittle, d.refusal);
}

// --- agenda -----------------------------------------------------------------

void test_refeicao_no_horario() {
  const Meal m{8, 0, 30, true, 0};
  TEST_ASSERT_TRUE(isMealDue(m, 2026327, 8 * 60));
  TEST_ASSERT_TRUE(isMealDue(m, 2026327, 8 * 60 + CATCHUP_MIN));
}

void test_refeicao_fora_da_janela() {
  const Meal m{8, 0, 30, true, 0};
  TEST_ASSERT_FALSE(isMealDue(m, 2026327, 7 * 60 + 59));
  TEST_ASSERT_FALSE(isMealDue(m, 2026327, 8 * 60 + CATCHUP_MIN + 1));
}

void test_refeicao_nao_repete_no_mesmo_dia() {
  const Meal m{8, 0, 30, true, 2026327};
  TEST_ASSERT_FALSE(isMealDue(m, 2026327, 8 * 60 + 1));
  TEST_ASSERT_TRUE(isMealDue(m, 2026328, 8 * 60 + 1));
}

void test_refeicao_desativada() {
  const Meal m{8, 0, 30, false, 0};
  TEST_ASSERT_FALSE(isMealDue(m, 2026327, 8 * 60));
}

// --- dispensa em malha fechada ------------------------------------------------

void test_dispensa_ate_o_alvo() {
  DispenseParams p;
  DispenseState s = startDispense(0, 30);

  Step st = nextStep(s, 0, p);
  TEST_ASSERT_EQUAL(Action::Forward, st.action);
  TEST_ASSERT_EQUAL_UINT32(p.longPulseMs, st.pulseMs);

  st = nextStep(s, 20, p);  // faltam 10 g: pulso curto
  TEST_ASSERT_EQUAL(Action::Forward, st.action);
  TEST_ASSERT_EQUAL_UINT32(p.shortPulseMs, st.pulseMs);

  st = nextStep(s, 28.5f, p);  // dentro da tolerância
  TEST_ASSERT_EQUAL(Action::Done, st.action);
}

void test_destrava_e_desiste_sem_progresso() {
  DispenseParams p;
  DispenseState s = startDispense(10, 40);

  TEST_ASSERT_EQUAL(Action::Forward, nextStep(s, 10, p).action);
  for (int i = 1; i < p.maxStalls; i++) {
    TEST_ASSERT_EQUAL(Action::Unjam, nextStep(s, 10, p).action);
  }
  TEST_ASSERT_EQUAL(Action::Stalled, nextStep(s, 10, p).action);
}

void test_progresso_zera_travamentos() {
  DispenseParams p;
  DispenseState s = startDispense(0, 100);
  nextStep(s, 0, p);
  nextStep(s, 0, p);  // 1 travamento
  TEST_ASSERT_EQUAL(1, s.stalls);
  nextStep(s, 10, p);
  TEST_ASSERT_EQUAL(0, s.stalls);
}

void test_teto_de_pulsos() {
  DispenseParams p;
  p.maxPulses = 3;
  DispenseState s = startDispense(0, 1000);
  float w = 0;
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL(Action::Forward, nextStep(s, w, p).action);
    w += 5;
  }
  TEST_ASSERT_EQUAL(Action::TooManyPulses, nextStep(s, w, p).action);
}

// --- vazão --------------------------------------------------------------------

void test_vazao_aprende_por_media_movel() {
  TEST_ASSERT_EQUAL_FLOAT(10, updateFlowRate(0, 20, 2000));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5 * 0.7f + 10 * 0.3f, updateFlowRate(5, 20, 2000));
}

void test_vazao_ignora_amostra_pobre() {
  TEST_ASSERT_EQUAL_FLOAT(5, updateFlowRate(5, 1, 2000));
  TEST_ASSERT_EQUAL_FLOAT(5, updateFlowRate(5, 20, 100));
}

void test_tempo_sem_balanca() {
  TEST_ASSERT_EQUAL_UINT32(3000, timedDispenseMs(30, 10));
  TEST_ASSERT_EQUAL_UINT32(MAX_TIMED_MS, timedDispenseMs(1000, 1));
  TEST_ASSERT_EQUAL_UINT32(0, timedDispenseMs(0, 10));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_alimenta_pote_vazio);
  RUN_TEST(test_recusa_pote_no_limite);
  RUN_TEST(test_alvo_nunca_passa_da_capacidade);
  RUN_TEST(test_recusa_quantidade_invalida);
  RUN_TEST(test_recusa_quando_cabe_muito_pouco);
  RUN_TEST(test_refeicao_no_horario);
  RUN_TEST(test_refeicao_fora_da_janela);
  RUN_TEST(test_refeicao_nao_repete_no_mesmo_dia);
  RUN_TEST(test_refeicao_desativada);
  RUN_TEST(test_dispensa_ate_o_alvo);
  RUN_TEST(test_destrava_e_desiste_sem_progresso);
  RUN_TEST(test_progresso_zera_travamentos);
  RUN_TEST(test_teto_de_pulsos);
  RUN_TEST(test_vazao_aprende_por_media_movel);
  RUN_TEST(test_vazao_ignora_amostra_pobre);
  RUN_TEST(test_tempo_sem_balanca);
  return UNITY_END();
}
