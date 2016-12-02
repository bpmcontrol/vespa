// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#include <vespa/fastos/fastos.h>
#include <vespa/log/log.h>
LOG_SETUP("featureoverride_test");
#include <vespa/vespalib/testkit/testapp.h>
#include <vespa/searchlib/fef/fef.h>

#include <vespa/searchlib/fef/test/indexenvironment.h>
#include <vespa/searchlib/fef/test/queryenvironment.h>
#include <vespa/searchlib/fef/test/plugin/double.h>
#include <vespa/searchlib/fef/test/plugin/sum.h>
#include <vespa/searchlib/features/valuefeature.h>

using namespace search::fef;
using namespace search::fef::test;
using namespace search::features;
using search::feature_t;

typedef Blueprint::SP       BPSP;

struct Fixture
{
    MatchDataLayout mdl;
    vespalib::Stash stash;
    std::vector<FeatureExecutor *> executors;
    MatchData::UP md;
    Fixture() : mdl(), executors(), md() {}
    Fixture &add(FeatureExecutor *executor, size_t outCnt) {
        executor->inputs_done();
        for (uint32_t outIdx = 0; outIdx < outCnt; ++outIdx) {
            executor->bindOutput(mdl.allocFeature());
        }
        executor->outputs_done();
        executors.push_back(executor);
        return *this;
    }
    Fixture &run() {
        md = mdl.createMatchData();
        for (const auto &executor : executors) {
            executor->execute(*md);
        }
        return *this;
    }
    feature_t resolveFeature(FeatureHandle handle) {
        return *md->resolveFeature(handle);
    }
    FeatureExecutor &createValueExecutor() {
        std::vector<feature_t> values;
        values.push_back(1.0);
        values.push_back(2.0);
        values.push_back(3.0);
        return stash.create<ValueExecutor>(values);
    }
};

TEST_F("test decorator - single override", Fixture)
{
    FeatureExecutor *fe = &f.createValueExecutor();
    fe = &f.stash.template create<FeatureOverrider>(*fe, 1, 50.0);
    f.add(fe, 3).run();
    EXPECT_EQUAL(fe->outputs().size(), 3u);

    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[0]), 1.0);
    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[1]), 50.0);
    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[2]), 3.0);
}

TEST_F("test decorator - multiple overrides", Fixture)
{
    FeatureExecutor *fe = &f.createValueExecutor();
    fe = &f.stash.template create<FeatureOverrider>(*fe, 0, 50.0);
    fe = &f.stash.template create<FeatureOverrider>(*fe, 2, 100.0);
    f.add(fe, 3).run();
    EXPECT_EQUAL(fe->outputs().size(), 3u);

    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[0]), 50.0);
    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[1]), 2.0);
    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[2]), 100.0);
}

TEST_F("test decorator - non-existing override", Fixture)
{
    FeatureExecutor *fe = &f.createValueExecutor();
    fe = &f.stash.template create<FeatureOverrider>(*fe, 1000, 50.0);
    f.add(fe, 3).run();
    EXPECT_EQUAL(fe->outputs().size(), 3u);

    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[0]), 1.0);
    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[1]), 2.0);
    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[2]), 3.0);
}

TEST_F("test decorator - transitive override", Fixture)
{
    FeatureExecutor::SharedInputs inputs;
    FeatureExecutor *fe = &f.createValueExecutor();
    fe = &f.stash.template create<FeatureOverrider>(*fe, 1, 50.0);
    f.add(fe, 3);
    EXPECT_EQUAL(fe->outputs().size(), 3u);

    FeatureExecutor *fe2 = &f.stash.template create<DoubleExecutor>(3);
    fe2->bind_shared_inputs(inputs);
    fe2->addInput(fe->outputs()[0]);
    fe2->addInput(fe->outputs()[1]);
    fe2->addInput(fe->outputs()[2]);
    fe2 = &f.stash.template create<FeatureOverrider>(*fe2, 2, 10.0);
    f.add(fe2, 3).run();
    EXPECT_EQUAL(fe2->outputs().size(), 3u);

    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[0]), 1.0);
    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[1]), 50.0);
    EXPECT_EQUAL(f.resolveFeature(fe->outputs()[2]), 3.0);
    EXPECT_EQUAL(f.resolveFeature(fe2->outputs()[0]), 2.0);
    EXPECT_EQUAL(f.resolveFeature(fe2->outputs()[1]), 100.0);
    EXPECT_EQUAL(f.resolveFeature(fe2->outputs()[2]), 10.0);
}

TEST("test overrides")
{
    BlueprintFactory bf;
    bf.addPrototype(BPSP(new ValueBlueprint()));
    bf.addPrototype(BPSP(new DoubleBlueprint()));
    bf.addPrototype(BPSP(new SumBlueprint()));

    IndexEnvironment idxEnv;
    RankSetup        rs(bf, idxEnv);

    rs.addDumpFeature("value(1,2,3)");
    rs.addDumpFeature("double(value(1))");
    rs.addDumpFeature("double(value(2))");
    rs.addDumpFeature("double(value(3))");
    rs.addDumpFeature("mysum(value(2),value(2))");
    rs.addDumpFeature("mysum(value(1),value(2),value(3))");
    EXPECT_TRUE(rs.compile());

    RankProgram::UP rankProgram = rs.create_dump_program();

    MatchDataLayout         mdl;
    QueryEnvironment        queryEnv;
    Properties              overrides;

    overrides.add("value(2)",       "20.0");
    overrides.add("value(1,2,3).1",  "4.0");
    overrides.add("value(1,2,3).2",  "6.0");
    overrides.add("bogus(feature)", "10.0");

    rankProgram->setup(mdl, queryEnv, overrides);
    rankProgram->run(2);

    std::map<vespalib::string, feature_t> res = Utils::getAllFeatures(*rankProgram);

    EXPECT_EQUAL(res.size(), 20u);
    EXPECT_APPROX(res["value(1)"],                               1.0, 1e-6);
    EXPECT_APPROX(res["value(1).0"],                             1.0, 1e-6);
    EXPECT_APPROX(res["value(2)"],                              20.0, 1e-6);
    EXPECT_APPROX(res["value(2).0"],                            20.0, 1e-6);
    EXPECT_APPROX(res["value(3)"],                               3.0, 1e-6);
    EXPECT_APPROX(res["value(3).0"],                             3.0, 1e-6);
    EXPECT_APPROX(res["value(1,2,3)"],                           1.0, 1e-6);
    EXPECT_APPROX(res["value(1,2,3).0"],                         1.0, 1e-6);
    EXPECT_APPROX(res["value(1,2,3).1"],                         4.0, 1e-6);
    EXPECT_APPROX(res["value(1,2,3).2"],                         6.0, 1e-6);
    EXPECT_APPROX(res["mysum(value(2),value(2))"],              40.0, 1e-6);
    EXPECT_APPROX(res["mysum(value(2),value(2)).out"],          40.0, 1e-6);
    EXPECT_APPROX(res["mysum(value(1),value(2),value(3))"],     24.0, 1e-6);
    EXPECT_APPROX(res["mysum(value(1),value(2),value(3)).out"], 24.0, 1e-6);
    EXPECT_APPROX(res["double(value(1))"],                       2.0, 1e-6);
    EXPECT_APPROX(res["double(value(1)).0"],                     2.0, 1e-6);
    EXPECT_APPROX(res["double(value(2))"],                      40.0, 1e-6);
    EXPECT_APPROX(res["double(value(2)).0"],                    40.0, 1e-6);
    EXPECT_APPROX(res["double(value(3))"],                       6.0, 1e-6);
    EXPECT_APPROX(res["double(value(3)).0"],                     6.0, 1e-6);
}

TEST_MAIN() { TEST_RUN_ALL(); }
