class TouchSlider {
    constructor() {
        this.slider = document.querySelector('.slider');
        this.slides = document.querySelectorAll('.slide');
        this.dots = document.querySelectorAll('.dot');
        this.scrollContainer = document.querySelector('.scroll-container');
        this.currentSlide = 0;
        this.startX = 0;
        this.startY = 0;
        this.isDragging = false;
        this.isScrolling = false;
        this.threshold = 50;

        this.initializeEvents();
    }

    initializeEvents() {
        // Touch Events
        this.slider.addEventListener('touchstart', (e) => {
            this.startX = e.touches[0].pageX;
            this.startY = e.touches[0].pageY;
            this.isDragging = true;
            this.isScrolling = false;
        }, { passive: true });
        
        this.slider.addEventListener('touchmove', (e) => {
            if (!this.isDragging) return;
            
            const currentX = e.touches[0].pageX;
            const currentY = e.touches[0].pageY;
            const diffX = currentX - this.startX;
            const diffY = currentY - this.startY;

            // Determine if scrolling or sliding based on direction
            if (!this.isScrolling && Math.abs(diffY) > Math.abs(diffX)) {
                this.isScrolling = true;
                this.isDragging = false;
                return;
            }

            if (!this.isScrolling && Math.abs(diffX) > 10) {
                //e.preventDefault();
                const offset = -this.currentSlide * 100 + (diffX / window.innerWidth * 100);
                if (offset <= 0 && offset >= -100) {
                    this.slider.style.transform = `translateX(${offset}vw)`;
                }
            }
        }, { passive: true });
        
        this.slider.addEventListener('touchend', (event) =>  {
            if (!this.isDragging) return;
            this.handleEnd();
        });

        // Mouse Events
        this.slider.addEventListener('mousedown', (e) => {
            // Only handle primary mouse button
            if (e.button !== 0) return;
            
            // Don't initiate slide if clicking inside scroll container
            if (e.target.closest('.scroll-container')) return;
            
            this.startX = e.pageX;
            this.isDragging = true;
            this.slider.style.transition = 'none';
            //e.preventDefault();       
        });

        this.slider.addEventListener('mousemove', (e) => {
            if (!this.isDragging) return;
            e.preventDefault();
            
            const diffX = e.pageX - this.startX;
            const offset = -this.currentSlide * 100 + (diffX / window.innerWidth * 100);
            
            if (offset <= 0 && offset >= -100) {
                this.slider.style.transform = `translateX(${offset}vw)`;
            }
        });

        this.slider.addEventListener('mouseup', () => this.handleEnd());
        this.slider.addEventListener('mouseleave', () => this.handleEnd());

        // Dot navigation
        this.dots.forEach((dot, index) => {
            dot.addEventListener('click', () => this.goToSlide(index));
        });
    }

    handleEnd() {
        if (!this.isDragging) return;
        
        this.isDragging = false;
        this.isScrolling = false;
        this.slider.style.transition = 'transform 0.3s ease-out';
        
        const currentX = parseFloat(this.slider.style.transform?.replace('translateX(', '') || 0);
        const movement = currentX + (this.currentSlide * 100);

        if (Math.abs(movement) > this.threshold / window.innerWidth * 100) {
            if (movement > 0 && this.currentSlide > 0) {
                this.currentSlide--;
            } else if (movement < 0 && this.currentSlide < this.slides.length - 1) {
                this.currentSlide++;
            }
        }

        this.updateSliderPosition();
    }

    goToSlide(index) {
        this.currentSlide = index;
        this.updateSliderPosition();
    }

    updateSliderPosition() {
        this.slider.style.transition = 'transform 0.3s ease-out';
        this.slider.style.transform = `translateX(-${this.currentSlide * 100}vw)`;

        if (this.currentSlide == 1) {
            window.scrollTo({ top: 0, behavior: 'smooth' });
        }
        
        this.dots.forEach((dot, index) => {
            dot.classList.toggle('active', index === this.currentSlide);
        });
    }
}