#pragma once

#include <QImage>
#include <QObject>

class ViewerTests : public QObject
{
    Q_OBJECT

private:
    static QImage fixture(int width = 96, int height = 64);

private slots:
    void initTestCase();
    void readsCommonFormats();
    void rotatesPngInPlaceWithMetadata();
    void rotatesExactJpegAndPreservesOddSizedPixels();
    void jpegHelperDeadlinePreservesOriginal();
    void savesJpegWithExistingExifOrientation();
    void rejectsAnimatedAndUnknownPngChunks();
    void preservesIndexedAndHighDepthPngPixels();
    void speculationSkipsLargePng();
    void refusesTruncatedJpegHelperOutput();
    void distinguishesSaveFailuresFromCommittedWarnings();
    void rejectsUnknownJpegMarkersAfterScan();
    void detectsExternalEditAndRejectsLinks();
    void catalogsKeepMatchingKeysAndPlaceholders();
    void translatorReloadsDuringLookups();
    void navigatesAndRecoversFromInvalidFiles();
    void retainsSnapshotOnFailedRefreshAndReconcilesDeletedCursor();
    void coalescesForegroundRequestsWhileReadIsHeld();
    void detailCanBeDroppedAndCannotBlockForeground();
    void heldScanCannotBlockOrReplaceNewFolder();
    void clipboardCopiesFullResolutionWithRotation();
    void localSendReceivesOneAbsolutePath();
    void portalRecoversFromCancelInvalidReplyAndServiceLoss();
    void failedRenderNeverKeepsAnOlderPhoto();
    void qmlContinuesOnlyAfterVerifiedSave();
    void benchmarkNavigation();
    void qmlShowsDockAndEnablesSaveOnlyForAnEdit();
    void qmlContextMenuCoversPicture();
    void qmlNativePinchUsesViewportZoom();
    void qmlExternalOpenProtectsEdits();
    void instanceForwardsLaunches();
    void instancePreservesFocusForForwardedImages();
};
