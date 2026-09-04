// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/canvas/retained_scene.hpp"
#include "aimora/studio/renderer/scene_surface.hpp"

#include <QApplication>
#include <QtTest>

namespace {

[[nodiscard]] std::shared_ptr<const aimora::studio::canvas::RetainedScene> makeScene() {
    using namespace aimora::studio::canvas;

    PrimitiveVector line;
    line.id = 17;
    line.kind = PrimitiveKind::Line;
    line.points = {{-60.0, 0.0}, {60.0, 0.0}};

    SceneTextSource text;
    text.id = 18;
    text.text = QStringLiteral("BUS A");
    text.position = QPointF{-20.0, -18.0};
    text.bounds = QRectF{-24.0, -30.0, 48.0, 18.0};

    SceneDocument document;
    document.generation = 73;
    document.styles.append(SceneStyle{});
    document.primitives.append(line);
    document.texts.append(text);
    document.page = ScenePageSource{19, QRectF{-100.0, -80.0, 200.0, 160.0}, Qt::white};
    return RetainedSceneCompiler{}.compile(document).scene;
}

} // namespace

class RendererTests final : public QObject {
    Q_OBJECT

  private slots:
    void softwareSurfaceRendersAndHitTestsRetainedScene();
    void redrawRemainsEventDrivenWhenStateIsIdle();
    void automaticBackendFallsBackForOffscreenRendering();
};

void RendererTests::softwareSurfaceRendersAndHitTestsRetainedScene() {
    using aimora::studio::renderer::RendererBackend;
    using aimora::studio::renderer::SceneSurface;

    const auto scene = makeScene();
    QVERIFY(scene != nullptr);
    SceneSurface surface{RendererBackend::SoftwarePainter};
    surface.resize(640, 360);
    surface.setScene(scene);
    surface.show();
    QCoreApplication::processEvents();

    QCOMPARE(static_cast<int>(surface.backend()),
             static_cast<int>(RendererBackend::SoftwarePainter));
    QCOMPARE(surface.statistics().sceneGeneration, quint64{73});
    QVERIFY(surface.statistics().renderedFrames > 0);
    const QImage image = surface.renderReferenceImage(QSize{640, 360}, 2.0);
    QVERIFY(!image.isNull());
    QVERIFY(surface.hitTest(QPointF{320.0, 180.0}, 3.0).contains(17));
}

void RendererTests::redrawRemainsEventDrivenWhenStateIsIdle() {
    using aimora::studio::renderer::RendererBackend;
    using aimora::studio::renderer::SceneSurface;

    SceneSurface surface{RendererBackend::SoftwarePainter};
    surface.resize(320, 240);
    surface.setScene(makeScene());
    surface.show();
    QCoreApplication::processEvents();
    const quint64 requestedFrames = surface.statistics().requestedFrames;
    const quint64 renderedFrames = surface.statistics().renderedFrames;

    for (int iteration = 0; iteration < 5; ++iteration) {
        QCoreApplication::processEvents();
    }

    QCOMPARE(surface.statistics().requestedFrames, requestedFrames);
    QCOMPARE(surface.statistics().renderedFrames, renderedFrames);
}

void RendererTests::automaticBackendFallsBackForOffscreenRendering() {
    using aimora::studio::renderer::RendererBackend;
    using aimora::studio::renderer::SceneSurface;

    SceneSurface surface{RendererBackend::Automatic};
    surface.resize(320, 240);
    surface.setScene(makeScene());
    surface.show();
    QCoreApplication::processEvents();
    QCOMPARE(static_cast<int>(surface.backend()),
             static_cast<int>(RendererBackend::SoftwarePainter));
}

QTEST_MAIN(RendererTests)

#include "renderer_tests.moc"
