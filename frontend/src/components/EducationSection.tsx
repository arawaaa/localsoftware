import { Typography, Container, Box } from '@mui/material';

const EducationSection = () => {
  return (
    <Box sx={{ position: 'relative', width: '100%' }}>
      {/* Vertical Label - Positioned absolutely in the left margin */}
      <Box 
        sx={{ 
          display: { xs: 'none', lg: 'flex' },
          position: 'absolute',
          left: { lg: 'calc(50% - 600px - 60px)', xl: 'calc(50% - 600px - 80px)' },
          top: 0,
          bottom: 0,
          width: '40px',
          pointerEvents: 'none',
        }}
      >
        <Box 
          sx={{ 
            position: 'sticky',
            top: 60,
            height: 'fit-content'
          }}
        >
          <Typography 
            variant="h3" 
            sx={{ 
              textTransform: 'uppercase', 
              fontWeight: 'bold', 
              color: 'primary.main',
              writingMode: 'vertical-rl',
              transform: 'rotate(180deg)',
              letterSpacing: 8,
              userSelect: 'none',
              whiteSpace: 'nowrap',
            }}
          >
            Education
          </Typography>
        </Box>
      </Box>

      <Container maxWidth="lg" sx={{ mb: 8 }}>
        <Box sx={{ width: '100%' }}>
          {/* Inline Label - visible on small screens */}
          <Typography 
            variant="h4" 
            sx={{ 
              display: { xs: 'block', lg: 'none' }, 
              fontWeight: 'bold', 
              mb: 3, 
              color: 'primary.main',
              textTransform: 'uppercase',
              letterSpacing: 2
            }}
          >
            Education
          </Typography>

          <Box sx={{ py: 2, mb: 4, borderBottom: '1px solid rgba(255, 255, 255, 0.08)' }}>
            <Typography variant="h5" component="h3" sx={{ fontWeight: 'bold', color: 'primary.main', mb: 1 }}>
              University of Illinois Urbana-Champaign
            </Typography>
            <Typography variant="h6" sx={{ fontWeight: 'medium', mb: 1, color: 'text.primary' }}>
              MS Biomedical Imaging
            </Typography>
            <Typography variant="body1" sx={{ fontSize: '1.1rem', color: 'text.secondary' }}>
              Incoming Graduate Student | Fall 2026
            </Typography>
          </Box>

          <Box sx={{ py: 2 }}>
            <Typography variant="h5" component="h3" sx={{ fontWeight: 'bold', color: 'primary.main', mb: 1 }}>
              University of Illinois Urbana-Champaign
            </Typography>
            <Typography variant="h6" sx={{ fontWeight: 'medium', mb: 1, color: 'text.primary' }}>
              BS Mathematics & Computer Science
            </Typography>
            <Typography variant="body1" sx={{ fontSize: '1.1rem', color: 'text.secondary', mb: 2 }}>
              High Distinction | GPA: 3.5 | 2021 – 2025
            </Typography>
            <Typography variant="body2" sx={{ fontWeight: 'bold', color: 'primary.main', textTransform: 'uppercase', letterSpacing: 1, mb: 1 }}>
              Relevant Coursework
            </Typography>
            <Box component="ul" sx={{ pl: 2, m: 0, '& li': { mb: 0.5, color: 'text.secondary' } }}>
              <li>Systems Programming</li>
              <li>Database Systems</li>
              <li>Computer Architecture</li>
              <li>Program Verification</li>
              <li>Machine Learning</li>
            </Box>
          </Box>
        </Box>
      </Container>
    </Box>
  );
};

export default EducationSection;
