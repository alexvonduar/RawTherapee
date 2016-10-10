/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "imagearea.h"
#include <ctime>
#include <cmath>
#include "options.h"
#include "multilangmgr.h"
#include "cropwindow.h"
#include "../rtengine/refreshmap.h"
#include "options.h"

ImageArea::ImageArea (ImageAreaPanel* p) : parent(p), firstOpen(true)
{

    infotext = "";
    cropgl = NULL;
    pmlistener = NULL;
    pmhlistener = NULL;
    focusGrabber = NULL;
    flawnOverWindow = NULL;
    mainCropWindow = NULL;
    previewHandler = NULL;
    showClippedH = false;
    showClippedS = false;
    listener = NULL;

    zoomPanel = Gtk::manage (new ZoomPanel (this));
    indClippedPanel = Gtk::manage (new IndicateClippedPanel (this));
    previewModePanel =  Gtk::manage (new PreviewModePanel (this));

    add_events(Gdk::LEAVE_NOTIFY_MASK);

    signal_size_allocate().connect( sigc::mem_fun(*this, &ImageArea::on_resized) );

    dirty = false;
    ipc = NULL;
    iLinkedImageArea = NULL;
}

ImageArea::~ImageArea ()
{

    for (auto cropWin : cropWins) {
        delete cropWin;
    }

    cropWins.clear ();

    if (mainCropWindow) {
        delete mainCropWindow;
    }
}

void ImageArea::on_realize()
{
    Gtk::DrawingArea::on_realize();

#if defined (__APPLE__)
    // Workaround: disabling POINTER_MOTION_HINT_MASK as for gtk 2.24.22 the get_pointer() function is buggy for quartz and modifier mask is not updated correctly.
    // This workaround should be removed when bug is fixed in GTK2 or when migrating to GTK3
    add_events(Gdk::EXPOSURE_MASK | Gdk::POINTER_MOTION_MASK | Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::SCROLL_MASK);
#else
    add_events(Gdk::EXPOSURE_MASK | Gdk::POINTER_MOTION_MASK | Gdk::POINTER_MOTION_HINT_MASK | Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::SCROLL_MASK);
#endif

    Cairo::FontOptions cfo;
    cfo.set_antialias (Cairo::ANTIALIAS_SUBPIXEL);
    get_pango_context ()->set_cairo_font_options (cfo);
}

void ImageArea::on_resized (Gtk::Allocation& req)
{
    if (ipc && get_width() > 1) { // sometimes on_resize is called in some init state, causing wrong sizes
        if (!mainCropWindow) {
            mainCropWindow = new CropWindow (this, false, false);
            mainCropWindow->setDecorated (false);
            mainCropWindow->setFitZoomEnabled (true);
            mainCropWindow->addCropWindowListener (this);
            mainCropWindow->setCropGUIListener (cropgl);
            mainCropWindow->setPointerMotionListener (pmlistener);
            mainCropWindow->setPointerMotionHListener (pmhlistener);
            mainCropWindow->setPosition (0, 0);
            mainCropWindow->setSize (get_width(), get_height());  // this execute the refresh itself
            mainCropWindow->enable();  // start processing !
        } else {
            mainCropWindow->setSize (get_width(), get_height());  // this execute the refresh itself
        }

        parent->syncBeforeAfterViews();
    }
}

rtengine::StagedImageProcessor* ImageArea::getImProcCoordinator() const
{
    return ipc;
}

void ImageArea::setImProcCoordinator(rtengine::StagedImageProcessor* ipc_)
{
    if( !ipc_ ) {
        focusGrabber = NULL;

        for (auto cropWin : cropWins) {
            delete cropWin;
        }

        cropWins.clear();

        mainCropWindow->deleteColorPickers ();
        mainCropWindow->setObservedCropWin (NULL);
    }

    ipc = ipc_;

}

void ImageArea::setPreviewHandler (PreviewHandler* ph)
{

    previewHandler = ph;
}

void ImageArea::on_style_updated ()
{

    // TODO: notify all crop windows that the style has been changed
    queue_draw ();
}

void ImageArea::setInfoText (Glib::ustring text)
{

    infotext = text;

    Glib::RefPtr<Pango::Context> context = get_pango_context () ;
    Pango::FontDescription fontd = context->get_font_description ();
    fontd.set_weight (Pango::WEIGHT_BOLD);
    fontd.set_size (10 * Pango::SCALE);
    context->set_font_description (fontd);
    ilayout = create_pango_layout("");
    ilayout->set_markup(text);
    int iw, ih;
    ilayout->get_pixel_size (iw, ih);
    ipixbuf = Gdk::Pixbuf::create (Gdk::COLORSPACE_RGB, true, 8, iw + 8, ih + 8);
    ipixbuf->fill (128);
}

void ImageArea::infoEnabled (bool e)
{

    if (options.showInfo != e) {
        options.showInfo = e;
        queue_draw ();
    }
}

CropWindow* ImageArea::getCropWindow (int x, int y)
{

    CropWindow* cw = mainCropWindow;

    for (auto cropWin : cropWins) {
        if (cropWin->isInside (x, y)) {
            return cropWin;
        }
    }

    return cw;
}

void ImageArea::redraw ()
{
    // dirty prevents multiple updates queued up
    if (!dirty) {
        dirty = true;
        queue_draw ();
    }
}

void ImageArea::switchPickerVisibility (bool isVisible)
{
    redraw();
}

bool ImageArea::on_draw(const ::Cairo::RefPtr< Cairo::Context> &cr)
{
    dirty = false;

    /* HOMBRE: How do we replace that??

    if (event->count) {
        return true;
    }

     */

    if (mainCropWindow) {
        mainCropWindow->expose (cr);
    }

    if (options.showInfo == true && infotext != "") {
        int fnw, fnh;
        ilayout->get_pixel_size (fnw, fnh);
        Gdk::Cairo::set_source_pixbuf(cr, ipixbuf, 4, 4);
        cr->rectangle(4, 4, fnw + 8, fnh + 8);
        cr->fill();
        cr->set_source_rgb (1.0, 1.0, 1.0);
        cr->move_to (8, 8);
        ilayout->add_to_cairo_context (cr);
        cr->fill ();
    }

    for (std::list<CropWindow*>::reverse_iterator i = cropWins.rbegin(); i != cropWins.rend(); ++i) {
        (*i)->expose (cr);
    }

    return true;
}


bool ImageArea::on_motion_notify_event (GdkEventMotion* event)
{

    if (focusGrabber) {
        focusGrabber->pointerMoved (event->state, event->x, event->y);
    } else {
        CropWindow* cw = getCropWindow (event->x, event->y);

        if (cw) {
            if (cw != flawnOverWindow) {
                if (flawnOverWindow) {
                    flawnOverWindow->flawnOver(false);
                }

                cw->flawnOver(true);
                flawnOverWindow = cw;
            }

            cw->pointerMoved (event->state, event->x, event->y);
        } else if (flawnOverWindow) {
            flawnOverWindow->flawnOver(false);
            flawnOverWindow = NULL;
        }
    }

    return true;
}

bool ImageArea::on_button_press_event (GdkEventButton* event)
{

    if (focusGrabber) {
        focusGrabber->buttonPress (event->button, event->type, event->state, event->x, event->y);
    } else {
        CropWindow* cw = getCropWindow (event->x, event->y);

        if (cw) {
            cw->buttonPress (event->button, event->type, event->state, event->x, event->y);
        }
    }

    return true;
}

bool ImageArea::on_scroll_event (GdkEventScroll* event)
{

    CropWindow* cw = getCropWindow (event->x, event->y);
    if (cw) {
        cw->scroll (event->state, event->direction, event->x, event->y);
    }

    return true;
}

bool ImageArea::on_button_release_event (GdkEventButton* event)
{

    if (focusGrabber) {
        focusGrabber->buttonRelease (event->button, event->type, event->state, event->x, event->y);
    } else {
        CropWindow* cw = getCropWindow (event->x, event->y);

        if (cw) {
            cw->buttonRelease (event->button, event->type, event->state, event->x, event->y);
        }
    }

    return true;
}

bool ImageArea::on_leave_notify_event  (GdkEventCrossing* event)
{
    if (flawnOverWindow) {
        flawnOverWindow->flawnOver(false);
        flawnOverWindow = NULL;
    }

    if (focusGrabber) {
        focusGrabber->flawnOver(false);
        focusGrabber->leaveNotify (event);
    } else {
        CropWindow* cw = getCropWindow (event->x, event->y);

        if (cw) {
            cw->flawnOver(false);
            cw->leaveNotify (event);
        }
    }

    return true;
}

void ImageArea::subscribe(EditSubscriber *subscriber)
{
    EditDataProvider::subscribe(subscriber);

    mainCropWindow->setEditSubscriber(subscriber);
    for (auto cropWin : cropWins) {
        cropWin->setEditSubscriber(subscriber);
    }

    if (listener && listener->getToolBar()) {
        listener->getToolBar()->startEditMode ();
    }

    if (subscriber && subscriber->getEditingType() == ET_OBJECTS) {
        // In this case, no need to reprocess the image, so we redraw the image to display the geometry
        queue_draw();
    }
}

void ImageArea::unsubscribe()
{
    bool wasObjectType = false;
    EditSubscriber*  oldSubscriber = EditDataProvider::getCurrSubscriber();

    if (oldSubscriber && oldSubscriber->getEditingType() == ET_OBJECTS) {
        wasObjectType = true;
    }

    EditDataProvider::unsubscribe();

    // Ask the Crops to free-up edit mode buffers
    mainCropWindow->setEditSubscriber(NULL);
    for (auto cropWin : cropWins) {
        cropWin->setEditSubscriber(NULL);
    }

    setToolHand();

    if (listener && listener->getToolBar()) {
        listener->getToolBar()->stopEditMode ();
    }

    if (wasObjectType) {
        queue_draw();
    }
}

void ImageArea::getImageSize (int &w, int&h)
{
    if (ipc) {
        w = ipc->getFullWidth();
        h = ipc->getFullHeight();
    } else {
        w = h = 0;
    }
}

void ImageArea::grabFocus (CropWindow* cw)
{

    focusGrabber = cw;

    if (cw && cw != mainCropWindow) {
        cropWindowSelected (cw);
    }
}

void ImageArea::unGrabFocus ()
{

    focusGrabber = NULL;
}

void ImageArea::addCropWindow ()
{
    if (!mainCropWindow) {
        return;    // if called but no image is loaded, it would crash
    }

    CropWindow* cw = new CropWindow (this, true, true);
    cw->zoom11();
    cw->setCropGUIListener (cropgl);
    cw->setPointerMotionListener (pmlistener);
    cw->setPointerMotionHListener (pmhlistener);
    int lastWidth = options.detailWindowWidth;
    int lastHeight = options.detailWindowHeight;

    if (lastWidth < lastHeight) {
        lastHeight = lastWidth;
    }

    if (lastHeight < lastWidth) {
        lastWidth = lastHeight;
    }

    if(!cropWins.empty()) {
        CropWindow *lastCrop;
        lastCrop = cropWins.front();

        if(lastCrop) {
            lastCrop->getSize(lastWidth, lastHeight);
        }
    }

    cropWins.push_front (cw);

    // Position the new crop window this way: start from top right going down to bottom. When bottom is reached, continue top left going down......
    int N = cropWins.size() - 1;
    int cropwidth, cropheight;

    if(lastWidth <= 0) { // this is only the case for very first start of RT 4.1 or when options file is deleted
        cropwidth = 200;
        cropheight = 200;
    } else {
        cropwidth = lastWidth;
        cropheight = lastHeight;
    }

    cw->setSize (cropwidth, cropheight);
    int x, y;
    int maxRows = get_height() / cropheight;

    if(maxRows == 0) {
        maxRows = 1;
    }

    int col = N / maxRows;

    if(col % 2) { // from left side
        col = col / 2;
        x = col * cropwidth;

        if(x >= get_width() - 50) {
            x = get_width() - 50;
        }
    } else {    // from right side
        col /= 2;
        col++;
        x = get_width() - col * cropwidth;

        if(x <= 0) {
            x = 0;
        }
    }

    y = cropheight * (N % maxRows);
    cw->setPosition (x, y);
    cw->setEditSubscriber (getCurrSubscriber());
    cw->enable(); // start processing!

    {
    int anchorX = 0;
    int anchorY = 0;
    mainCropWindow->getCropAnchorPosition(anchorX, anchorY);
    cw->setCropAnchorPosition(anchorX, anchorY);
    }

    mainCropWindow->setObservedCropWin (cropWins.front());

    if(cropWins.size() == 1) { // after first detail window we already have high quality
        ipc->startProcessing(M_HIGHQUAL);
    }
}


void ImageArea::cropWindowSelected (CropWindow* cw)
{

    std::list<CropWindow*>::iterator i = std::find (cropWins.begin(), cropWins.end(), cw);

    if (i != cropWins.end()) {
        cropWins.erase (i);
    }

    cropWins.push_front (cw);
    mainCropWindow->setObservedCropWin (cropWins.front());
}

void ImageArea::cropWindowClosed (CropWindow* cw)
{

    focusGrabber = NULL;
    std::list<CropWindow*>::iterator i = std::find (cropWins.begin(), cropWins.end(), cw);

    if (i != cropWins.end()) {
        cropWins.erase (i);
    }

    if (!cropWins.empty()) {
        mainCropWindow->setObservedCropWin (cropWins.front());
    } else {
        mainCropWindow->setObservedCropWin (NULL);
    }

    queue_draw ();
}

void ImageArea::straightenReady (double rotDeg)
{

    if (listener) {
        listener->rotateSelectionReady (rotDeg);
    }
}

void ImageArea::spotWBSelected (int x, int y)
{

    if (listener) {
        listener->spotWBselected (x, y);
    }
}

void ImageArea::getScrollImageSize (int& w, int& h)
{

    if (mainCropWindow && ipc) {
        w = ipc->getFullWidth();
        h = ipc->getFullHeight();
    } else {
        w = h = 0;
    }
}

void ImageArea::getScrollPosition (int& x, int& y)
{

    if (mainCropWindow) {
        mainCropWindow->getCropAnchorPosition (x, y);
    } else {
        x = y = 0;
    }
}

void ImageArea::setScrollPosition (int x, int y)
{

    if (mainCropWindow) {
        mainCropWindow->delCropWindowListener (this);
        mainCropWindow->setCropAnchorPosition (x, y);
        mainCropWindow->addCropWindowListener (this);
    }
}

void ImageArea::cropPositionChanged (CropWindow* cw)
{

    syncBeforeAfterViews ();
}

void ImageArea::cropWindowSizeChanged (CropWindow* cw)
{

    syncBeforeAfterViews ();
}

void ImageArea::cropZoomChanged (CropWindow* cw)
{

    if (cw == mainCropWindow) {
        parent->zoomChanged ();
        syncBeforeAfterViews ();
        zoomPanel->refreshZoomLabel ();
    }
}

double ImageArea::getZoom ()
{

    if (mainCropWindow) {
        return mainCropWindow->getZoom ();
    } else {
        return 1.0;
    }
}

// Called by imageAreaPanel before/after views
void ImageArea::setZoom (double zoom)
{

    if (mainCropWindow) {
        mainCropWindow->setZoom (zoom);
    }

    zoomPanel->refreshZoomLabel ();
}

void ImageArea::initialImageArrived (CropWindow* cw)
{

    if (mainCropWindow) {
        if(firstOpen || options.prevdemo != PD_Sidecar || (!options.rememberZoomAndPan) ) {
            mainCropWindow->zoomFit ();
            firstOpen = false;
            mainCropWindow->cropHandler.getFullImageSize(fullImageWidth, fullImageHeight);
        } else {
            int w, h;
            mainCropWindow->cropHandler.getFullImageSize(w, h);

            if(w != fullImageWidth || h != fullImageHeight) {
                mainCropWindow->zoomFit ();
            }

            fullImageWidth = w;
            fullImageHeight = h;
        }
    }
}

void ImageArea::syncBeforeAfterViews ()
{
    parent->syncBeforeAfterViews ();
}

void ImageArea::setCropGUIListener (CropGUIListener* l)
{

    cropgl = l;

    for (auto cropWin : cropWins) {
        cropWin->setCropGUIListener (cropgl);
    }

    if (mainCropWindow) {
        mainCropWindow->setCropGUIListener (cropgl);
    }
}

void ImageArea::setPointerMotionListener (PointerMotionListener* pml)
{

    pmlistener = pml;

    for (auto cropWin : cropWins) {
        cropWin->setPointerMotionListener (pml);
    }

    if (mainCropWindow) {
        mainCropWindow->setPointerMotionListener (pml);
    }
}

void ImageArea::setPointerMotionHListener (PointerMotionListener* pml)
{

    pmhlistener = pml;

    for (auto cropWin : cropWins) {
        cropWin->setPointerMotionHListener (pml);
    }

    if (mainCropWindow) {
        mainCropWindow->setPointerMotionHListener (pml);
    }
}

ToolMode ImageArea::getToolMode ()
{

    if (listener && listener->getToolBar()) {
        return listener->getToolBar()->getTool ();
    } else {
        return TMHand;
    }
}

bool ImageArea::showColorPickers ()
{

    if (listener && listener->getToolBar()) {
        return listener->getToolBar()->showColorPickers ();
    } else {
        return false;
    }
}

void ImageArea::setToolHand ()
{

    if (listener && listener->getToolBar()) {
        listener->getToolBar()->setTool (TMHand);
    }
}

int ImageArea::getSpotWBRectSize  ()
{

    if (listener) {
        return listener->getSpotWBRectSize ();
    } else {
        return 1;
    }
}
